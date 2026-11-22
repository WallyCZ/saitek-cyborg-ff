#include "proppage.h"
#include "effectdriver.h"
#include "resource.h"
#include "settings.h"
#include "log.h"
#include "devices.h"
#include <commctrl.h>
#include <prsht.h>

#pragma comment(lib, "dinput8.lib")

namespace {

// Test effects bound to the stick's buttons. The stick itself plays them when a button is pressed
// (DirectInput trigger buttons), so they only need to be downloaded while the page is open.
class TestEffects {
public:
    ~TestEffects() { Stop(); }
    enum Result { Armed, NotAvailable, Refused };
    Result Start(HWND window);
    void Stop();
    bool Active() const { return device_ != nullptr; }
    static BOOL CALLBACK FindStick(LPCDIDEVICEINSTANCEW instance, LPVOID context);

private:
    void Add(const GUID& type, LONG direction, DWORD durationMs, DWORD button, DWORD repeatMs, void* params,
             DWORD paramSize, DIENVELOPE* envelope = nullptr);

    IDirectInput8W* di_ = nullptr;
    IDirectInputDevice8W* device_ = nullptr;
    std::vector<IDirectInputEffect*> effects_;
};

TestEffects g_test;  // joy.cpl shows one property sheet at a time

// Sticks with force motors (Evo Force) have springs, dampers and a centering spring; the 3D Rumble Force only
// has a rumble motor. Registration lists spring effects only for sticks that can play them.
bool g_forceMotors = true;

bool StickHasForceMotors()
{
    IDirectInput8W* di;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                  reinterpret_cast<void**>(&di), nullptr)))
        return true;
    bool force = true;  // no stick found: keep everything available
    GUID stick = GUID_NULL;
    IDirectInputDevice8W* dev;
    if (SUCCEEDED(di->EnumDevices(DI8DEVCLASS_GAMECTRL, TestEffects::FindStick, &stick, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK)) &&
        stick != GUID_NULL && SUCCEEDED(di->CreateDevice(stick, &dev, nullptr))) {
        DIEFFECTINFOW info = { sizeof(info) };
        force = SUCCEEDED(dev->GetEffectInfo(&info, GUID_Spring));
        dev->Release();
    }
    di->Release();
    return force;
}

BOOL CALLBACK TestEffects::FindStick(LPCDIDEVICEINSTANCEW instance, LPVOID context)
{
    // DirectInput product GUIDs of HID devices start with the product and vendor id
    for (const auto& d : g_devices)
        if (instance->guidProduct.Data1 == (DWORD)MAKELONG(d.vendorId, d.productId)) {
            *static_cast<GUID*>(context) = instance->guidInstance;
            return DIENUM_STOP;
        }
    return DIENUM_CONTINUE;
}

TestEffects::Result TestEffects::Start(HWND window)
{
    Stop();
    GUID stick = GUID_NULL;
    HRESULT hr = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                    reinterpret_cast<void**>(&di_), nullptr);
    if (SUCCEEDED(hr))
        hr = di_->EnumDevices(DI8DEVCLASS_GAMECTRL, FindStick, &stick, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    if (SUCCEEDED(hr))
        hr = stick == GUID_NULL ? DIERR_DEVICENOTREG : di_->CreateDevice(stick, &device_, nullptr);
    // Force feedback needs exclusive access, which fails while a game holds the stick
    if (SUCCEEDED(hr))
        hr = device_->SetDataFormat(&c_dfDIJoystick);
    if (SUCCEEDED(hr))
        hr = device_->SetCooperativeLevel(GetAncestor(window, GA_ROOT), DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    if (SUCCEEDED(hr))
        hr = device_->Acquire();
    if (FAILED(hr)) {
        Log("test effects: stick not available for force feedback (%08lX)", hr);
        Stop();
        return NotAvailable;
    }
    DICONSTANTFORCE kick = { 7000 }, blast = { 10000 };
    DIPERIODIC engine = { 3500, 0, 0, 40 * 1000 }, road = { 6000, 0, 0, 250 * 1000 };
    DIENVELOPE fade = { sizeof(DIENVELOPE), 10000, 0, 0, 800 * 1000 };
    Add(GUID_ConstantForce, 0, 40, 0, 110, &kick, sizeof(kick));                         // machine gun, repeats while held
    Add(GUID_ConstantForce, 13500, 900, 1, INFINITE, &blast, sizeof(blast), &fade);       // explosion
    Add(GUID_Sine, 9000, 1500, 2, INFINITE, &engine, sizeof(engine));                     // engine rumble
    Add(GUID_Square, 0, 1500, 3, INFINITE, &road, sizeof(road));                          // bumpy road
    // One condition per axis, so the Springs and Dampers sliders can be tried as well
    if (g_forceMotors) {
        DICONDITION spring[2] = { { 0, 8000, 8000, 10000, 10000, 0 }, { 0, 8000, 8000, 10000, 10000, 0 } };
        DICONDITION damper[2] = { { 0, 10000, 10000, 10000, 10000, 0 }, { 0, 10000, 10000, 10000, 10000, 0 } };
        Add(GUID_Spring, 0, 3000, 4, INFINITE, spring, sizeof(spring));                   // spring
        Add(GUID_Damper, 0, 3000, 5, INFINITE, damper, sizeof(damper));                   // damper
    }
    Log("test effects armed: %zu", effects_.size());
    if (effects_.empty()) {
        Stop();
        return Refused;
    }
    return Armed;
}

void TestEffects::Add(const GUID& type, LONG direction, DWORD durationMs, DWORD button, DWORD repeatMs, void* params,
                      DWORD paramSize, DIENVELOPE* envelope)
{
    DWORD axes[2] = { DIJOFS_X, DIJOFS_Y };
    LONG dir[2] = { direction, 0 };
    DIEFFECT e = { sizeof(e) };
    bool perAxis = paramSize == 2 * sizeof(DICONDITION);
    e.dwFlags = (perAxis ? DIEFF_CARTESIAN : DIEFF_POLAR) | DIEFF_OBJECTOFFSETS;
    e.dwDuration = durationMs * 1000;
    e.dwGain = DI_FFNOMINALMAX;
    e.dwTriggerButton = DIJOFS_BUTTON(button);
    e.dwTriggerRepeatInterval = repeatMs == INFINITE ? INFINITE : repeatMs * 1000;
    e.cAxes = 2;
    e.rgdwAxes = axes;
    e.rglDirection = dir;
    e.lpEnvelope = envelope;
    e.cbTypeSpecificParams = paramSize;
    e.lpvTypeSpecificParams = params;
    IDirectInputEffect* effect;
    HRESULT hr = device_->CreateEffect(type, &e, &effect, nullptr);
    if (SUCCEEDED(hr))
        effects_.push_back(effect);
    else
        Log("test effect for button %lu refused by DirectInput (%08lX)", button + 1, hr);
}

void TestEffects::Stop()
{
    for (auto effect : effects_)
        effect->Release();
    effects_.clear();
    if (device_) {
        device_->Unacquire();
        device_->Release();
        device_ = nullptr;
    }
    if (di_) {
        di_->Release();
        di_ = nullptr;
    }
}

struct Slider {
    int control, value;
};
constexpr Slider Sliders[] = {
    { IDC_GAIN, IDC_GAIN_VALUE },
    { IDC_SPRING, IDC_SPRING_VALUE },
    { IDC_DAMPER, IDC_DAMPER_VALUE },
    { IDC_CENTERING_STRENGTH, IDC_CENTERING_VALUE },
};

void ShowValue(HWND page, const Slider& s)
{
    wchar_t text[16];
    swprintf_s(text, L"%d %%", (int)SendDlgItemMessageW(page, s.control, TBM_GETPOS, 0, 0));
    SetDlgItemTextW(page, s.value, text);
}

void UpdateCenteringControls(HWND page)
{
    EnableWindow(GetDlgItem(page, IDC_CENTERING), g_forceMotors);
    BOOL on = g_forceMotors && IsDlgButtonChecked(page, IDC_CENTERING) == BST_CHECKED;
    for (int id : { IDC_CENTERING_LABEL, IDC_CENTERING_STRENGTH, IDC_CENTERING_VALUE })
        EnableWindow(GetDlgItem(page, id), on);
}

// Grays out what a rumble stick cannot do and says so in the test text
void ShowStickCapabilities(HWND page)
{
    for (int id : { IDC_SPRING_LABEL, IDC_SPRING, IDC_SPRING_VALUE, IDC_DAMPER_LABEL, IDC_DAMPER, IDC_DAMPER_VALUE })
        EnableWindow(GetDlgItem(page, id), g_forceMotors);
    if (!g_forceMotors)
        SetDlgItemTextW(page, IDC_TEST_STATUS,
                        L"Trigger: machine gun, 2: explosion, 3: engine rumble, 4: bumpy road. This stick has a rumble "
                        L"motor, so springs, dampers and the centering spring are not available.");
    UpdateCenteringControls(page);
}

void Show(HWND page, const Settings& s)
{
    SendDlgItemMessageW(page, IDC_GAIN, TBM_SETPOS, TRUE, s.gain);
    SendDlgItemMessageW(page, IDC_SPRING, TBM_SETPOS, TRUE, s.springGain);
    SendDlgItemMessageW(page, IDC_DAMPER, TBM_SETPOS, TRUE, s.damperGain);
    SendDlgItemMessageW(page, IDC_CENTERING_STRENGTH, TBM_SETPOS, TRUE, s.centeringStrength);
    CheckDlgButton(page, IDC_CENTERING, s.centering ? BST_CHECKED : BST_UNCHECKED);
    for (const auto& slider : Sliders)
        ShowValue(page, slider);
    UpdateCenteringControls(page);
}

Settings Read(HWND page)
{
    Settings s;
    s.gain = (DWORD)SendDlgItemMessageW(page, IDC_GAIN, TBM_GETPOS, 0, 0);
    s.springGain = (DWORD)SendDlgItemMessageW(page, IDC_SPRING, TBM_GETPOS, 0, 0);
    s.damperGain = (DWORD)SendDlgItemMessageW(page, IDC_DAMPER, TBM_GETPOS, 0, 0);
    s.centeringStrength = (DWORD)SendDlgItemMessageW(page, IDC_CENTERING_STRENGTH, TBM_GETPOS, 0, 0);
    s.centering = IsDlgButtonChecked(page, IDC_CENTERING) == BST_CHECKED;
    return s;
}

INT_PTR CALLBACK PageProc(HWND page, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
        for (const auto& slider : Sliders) {
            SendDlgItemMessageW(page, slider.control, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendDlgItemMessageW(page, slider.control, TBM_SETTICFREQ, 10, 0);
            SendDlgItemMessageW(page, slider.control, TBM_SETPAGESIZE, 0, 10);
        }
        g_forceMotors = StickHasForceMotors();
        Show(page, Settings::Load());
        ShowStickCapabilities(page);
        return TRUE;
    case WM_HSCROLL:
        for (const auto& slider : Sliders)
            if ((HWND)lParam == GetDlgItem(page, slider.control))
                ShowValue(page, slider);
        PropSheet_Changed(GetParent(page), page);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_CENTERING && HIWORD(wParam) == BN_CLICKED) {
            UpdateCenteringControls(page);
            PropSheet_Changed(GetParent(page), page);
        } else if (LOWORD(wParam) == IDC_DEFAULTS && HIWORD(wParam) == BN_CLICKED) {
            Show(page, Settings{});
            PropSheet_Changed(GetParent(page), page);
        } else if (LOWORD(wParam) == IDC_TEST_EFFECTS && HIWORD(wParam) == BN_CLICKED) {
            if (IsDlgButtonChecked(page, IDC_TEST_EFFECTS) != BST_CHECKED) {
                g_test.Stop();
            } else if (auto result = g_test.Start(page); result != TestEffects::Armed) {
                CheckDlgButton(page, IDC_TEST_EFFECTS, BST_UNCHECKED);
                MessageBoxW(page,
                            result == TestEffects::NotAvailable
                                ? L"The stick is not available for force feedback. Close any game that uses it and try again."
                                : L"The stick refused the test effects. Please report this at "
                                  L"https://github.com/WallyCZ/saitek-cyborg-ff/issues with the debug log (see the README).",
                            L"Force Feedback", MB_ICONWARNING);
            }
        }
        return TRUE;
    case WM_DESTROY:
        g_test.Stop();
        return FALSE;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lParam)->code == PSN_APPLY) {
            Settings s = Read(page);
            s.Save();
            ApplyCentering(s);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

}  // namespace

PropSheet::PropSheet(HINSTANCE module) : module_(module)
{
    InterlockedIncrement(&g_objects);
    // Keep Windows' own Test and Settings (calibration) pages: the sheet we register replaces the default one
    if (FAILED(CoCreateInstance(CLSID_GameControllerDefaultSheet, nullptr, CLSCTX_INPROC_SERVER, IID_IDIGameCntrlPropSheet,
                                reinterpret_cast<void**>(&defaultSheet_))))
        defaultSheet_ = nullptr;
}

PropSheet::~PropSheet()
{
    if (defaultSheet_)
        defaultSheet_->Release();
    InterlockedDecrement(&g_objects);
}

STDMETHODIMP PropSheet::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDIGameCntrlPropSheet) {
        *ppv = static_cast<IDIGameCntrlPropSheet*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) PropSheet::AddRef() { return InterlockedIncrement(&refs_); }

STDMETHODIMP_(ULONG) PropSheet::Release()
{
    ULONG refs = InterlockedDecrement(&refs_);
    if (!refs)
        delete this;
    return refs;
}

STDMETHODIMP PropSheet::SetID(USHORT id)
{
    id_ = id;
    if (defaultSheet_)
        defaultSheet_->SetID(id);
    return S_OK;
}

void PropSheet::Build()
{
    if (built_)
        return;
    built_ = true;
    sheet_.dwSize = sizeof(sheet_);
    sheet_.lpwszSheetCaption = const_cast<LPWSTR>(L"Saitek Cyborg");
    DIGCSHEETINFO* defaults = nullptr;
    DIGCPAGEINFO* defaultPages = nullptr;
    if (defaultSheet_ && SUCCEEDED(defaultSheet_->GetSheetInfo(&defaults)) && defaults &&
        SUCCEEDED(defaultSheet_->GetPageInfo(&defaultPages)) && defaultPages) {
        sheet_ = *defaults;
        pages_.assign(defaultPages, defaultPages + defaults->nNumPages);
    }
    DIGCPAGEINFO page = { sizeof(DIGCPAGEINFO) };
    page.lpwszPageTitle = const_cast<LPWSTR>(L"Force Feedback");
    page.fpPageProc = PageProc;
    page.lpwszTemplate = MAKEINTRESOURCEW(IDD_FORCE);
    page.hInstance = module_;
    pages_.push_back(page);
    sheet_.nNumPages = (USHORT)pages_.size();
    Log("property sheet for joystick %u: %u pages", id_, sheet_.nNumPages);
}

STDMETHODIMP PropSheet::GetSheetInfo(DIGCSHEETINFO** sheet)
{
    if (!sheet)
        return E_POINTER;
    Build();
    *sheet = &sheet_;
    return S_OK;
}

STDMETHODIMP PropSheet::GetPageInfo(DIGCPAGEINFO** pages)
{
    if (!pages)
        return E_POINTER;
    Build();
    *pages = pages_.data();
    return S_OK;
}
