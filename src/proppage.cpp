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
    bool Start(HWND window);
    void Stop();
    bool Active() const { return device_ != nullptr; }

private:
    static BOOL CALLBACK FindStick(LPCDIDEVICEINSTANCEW instance, LPVOID context);
    void Add(const GUID& type, LONG direction, DWORD durationMs, DWORD button, DWORD repeatMs, void* params,
             DWORD paramSize, DIENVELOPE* envelope = nullptr);

    IDirectInput8W* di_ = nullptr;
    IDirectInputDevice8W* device_ = nullptr;
    std::vector<IDirectInputEffect*> effects_;
};

TestEffects g_test;  // joy.cpl shows one property sheet at a time

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

bool TestEffects::Start(HWND window)
{
    Stop();
    GUID stick = GUID_NULL;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                  reinterpret_cast<void**>(&di_), nullptr)) ||
        FAILED(di_->EnumDevices(DI8DEVCLASS_GAMECTRL, FindStick, &stick, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK)) ||
        stick == GUID_NULL || FAILED(di_->CreateDevice(stick, &device_, nullptr))) {
        Stop();
        return false;
    }
    // Force feedback needs exclusive access, which fails while a game holds the stick
    if (FAILED(device_->SetDataFormat(&c_dfDIJoystick)) ||
        FAILED(device_->SetCooperativeLevel(GetAncestor(window, GA_ROOT), DISCL_EXCLUSIVE | DISCL_BACKGROUND)) ||
        FAILED(device_->Acquire())) {
        Stop();
        return false;
    }
    DICONSTANTFORCE kick = { 7000 }, blast = { 10000 };
    DIPERIODIC engine = { 3500, 0, 0, 40 * 1000 }, road = { 6000, 0, 0, 250 * 1000 };
    DIENVELOPE fade = { sizeof(DIENVELOPE), 10000, 0, 0, 800 * 1000 };
    Add(GUID_ConstantForce, 0, 40, 0, 110, &kick, sizeof(kick));                         // machine gun, repeats while held
    Add(GUID_ConstantForce, 13500, 900, 1, INFINITE, &blast, sizeof(blast), &fade);       // explosion
    Add(GUID_Sine, 9000, 1500, 2, INFINITE, &engine, sizeof(engine));                     // engine rumble
    Add(GUID_Square, 0, 1500, 3, INFINITE, &road, sizeof(road));                          // bumpy road
    Log("test effects armed: %zu", effects_.size());
    return !effects_.empty();
}

void TestEffects::Add(const GUID& type, LONG direction, DWORD durationMs, DWORD button, DWORD repeatMs, void* params,
                      DWORD paramSize, DIENVELOPE* envelope)
{
    DWORD axes[2] = { DIJOFS_X, DIJOFS_Y };
    LONG dir[2] = { direction, 0 };
    DIEFFECT e = { sizeof(e) };
    e.dwFlags = DIEFF_POLAR | DIEFF_OBJECTOFFSETS;
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
    if (SUCCEEDED(device_->CreateEffect(type, &e, &effect, nullptr)))
        effects_.push_back(effect);
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
    BOOL on = IsDlgButtonChecked(page, IDC_CENTERING) == BST_CHECKED;
    for (int id : { IDC_CENTERING_LABEL, IDC_CENTERING_STRENGTH, IDC_CENTERING_VALUE })
        EnableWindow(GetDlgItem(page, id), on);
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
        Show(page, Settings::Load());
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
            } else if (!g_test.Start(page)) {
                CheckDlgButton(page, IDC_TEST_EFFECTS, BST_UNCHECKED);
                MessageBoxW(page, L"The stick is not available for force feedback. Close any game that uses it and try again.",
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
