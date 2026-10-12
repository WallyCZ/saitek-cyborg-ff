#include <initguid.h>
#include "effectdriver.h"
#include "proppage.h"
#include "devices.h"
#include "settings.h"
#include "log.h"
#include <stdio.h>
#include <string>

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

LONG g_objects;
static HMODULE g_module;

namespace {

// One factory per COM class: the effect driver for DirectInput, the property sheet for joy.cpl
template <class T>
class ClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        T* object = Create();
        if (!object)
            return E_OUTOFMEMORY;
        HRESULT hr = object->QueryInterface(riid, ppv);
        object->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override
    {
        if (lock)
            InterlockedIncrement(&g_objects);
        else
            InterlockedDecrement(&g_objects);
        return S_OK;
    }

private:
    static T* Create();
};

template <> EffectDriver* ClassFactory<EffectDriver>::Create() { return new (std::nothrow) EffectDriver(); }
template <> PropSheet* ClassFactory<PropSheet>::Create() { return new (std::nothrow) PropSheet(g_module); }

ClassFactory<EffectDriver> g_driverFactory;
ClassFactory<PropSheet> g_sheetFactory;

constexpr wchar_t OemKey[] = L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\";

// DirectInput DIEFFECTATTRIBUTES as stored in the registry. dwEffectId is what DirectInput passes back to
// DownloadEffect: we use the HID usage of the effect type.
struct EffectAttributes {
    DWORD effectId, effectType, staticParams, dynamicParams, coords;
};

struct EffectInfo {
    const wchar_t* guid;
    const wchar_t* name;
    EffectAttributes attributes;
};

constexpr DWORD CommonFlags = DIEFT_FFATTACK | DIEFT_FFFADE | DIEFT_STARTDELAY;
constexpr DWORD ConditionFlags = DIEFT_SATURATION | DIEFT_POSNEGCOEFFICIENTS | DIEFT_POSNEGSATURATION |
                                 DIEFT_DEADBAND | DIEFT_STARTDELAY;
constexpr DWORD AllParams = DIEP_DURATION | DIEP_GAIN | DIEP_TRIGGERBUTTON | DIEP_TRIGGERREPEATINTERVAL | DIEP_AXES |
                            DIEP_DIRECTION | DIEP_ENVELOPE | DIEP_TYPESPECIFICPARAMS | DIEP_STARTDELAY;
constexpr DWORD ConditionParams = AllParams & ~DIEP_ENVELOPE;

const EffectInfo g_effects[] = {
    { L"{13541C20-8E33-11D0-9AD0-00A0C9A06E35}", L"Constant Force",
      { usage::ConstantForce, DIEFT_CONSTANTFORCE | CommonFlags, AllParams, AllParams, DIEFF_POLAR } },
    { L"{13541C21-8E33-11D0-9AD0-00A0C9A06E35}", L"Ramp Force",
      { usage::Ramp, DIEFT_RAMPFORCE | CommonFlags, AllParams, AllParams, DIEFF_POLAR } },
    { L"{13541C22-8E33-11D0-9AD0-00A0C9A06E35}", L"Square Wave",
      { usage::Square, DIEFT_PERIODIC | CommonFlags, AllParams, AllParams, DIEFF_POLAR } },
    { L"{13541C23-8E33-11D0-9AD0-00A0C9A06E35}", L"Sine Wave",
      { usage::Sine, DIEFT_PERIODIC | CommonFlags, AllParams, AllParams, DIEFF_POLAR } },
    { L"{13541C24-8E33-11D0-9AD0-00A0C9A06E35}", L"Triangle Wave",
      { usage::Triangle, DIEFT_PERIODIC | CommonFlags, AllParams, AllParams, DIEFF_POLAR } },
    { L"{13541C25-8E33-11D0-9AD0-00A0C9A06E35}", L"Sawtooth Up Wave",
      { usage::SawtoothUp, DIEFT_PERIODIC | CommonFlags, AllParams, AllParams, DIEFF_POLAR } },
    { L"{13541C26-8E33-11D0-9AD0-00A0C9A06E35}", L"Sawtooth Down Wave",
      { usage::SawtoothDown, DIEFT_PERIODIC | CommonFlags, AllParams, AllParams, DIEFF_POLAR } },
    { L"{13541C27-8E33-11D0-9AD0-00A0C9A06E35}", L"Spring",
      { usage::Spring, DIEFT_CONDITION | ConditionFlags, ConditionParams, ConditionParams, DIEFF_POLAR } },
    { L"{13541C28-8E33-11D0-9AD0-00A0C9A06E35}", L"Damper",
      { usage::Damper, DIEFT_CONDITION | ConditionFlags, ConditionParams, ConditionParams, DIEFF_POLAR } },
    { L"{13541C29-8E33-11D0-9AD0-00A0C9A06E35}", L"Inertia",
      { usage::Inertia, DIEFT_CONDITION | ConditionFlags, ConditionParams, ConditionParams, DIEFF_POLAR } },
    { L"{13541C2A-8E33-11D0-9AD0-00A0C9A06E35}", L"Friction",
      { usage::Friction, DIEFT_CONDITION | ConditionFlags, ConditionParams, ConditionParams, DIEFF_POLAR } },
};

LSTATUS SetString(HKEY key, const wchar_t* name, const wchar_t* value)
{
    return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value, (DWORD)(wcslen(value) + 1) * sizeof(wchar_t));
}

std::wstring GuidString(REFGUID guid)
{
    wchar_t text[64];
    StringFromGUID2(guid, text, ARRAYSIZE(text));
    return text;
}

// Writes the stick's DirectInput OEM data into HKLM or HKCU: OEMForceFeedback (our effect driver and its
// effects) and ConfigCLSID (our property sheet in the Game Controllers panel). DirectInput keeps a per-user
// copy of the OEM data in HKCU, which Windows fills with its generic PID driver when the stick is first
// plugged in.
LSTATUS RegisterDevice(HKEY root, const wchar_t* device)
{
    wchar_t oem[256], path[256];
    swprintf_s(oem, L"%s%s", OemKey, device);
    swprintf_s(path, L"%s\\OEMForceFeedback", oem);
    RegDeleteTreeW(root, path);

    HKEY key;
    LSTATUS rc = RegCreateKeyExW(root, oem, 0, nullptr, 0, KEY_WRITE | KEY_QUERY_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;
    SetString(key, L"ConfigCLSID", GuidString(CLSID_SaitekFFConfig).c_str());
    // DirectInput only reports the joystick type (and so our ConfigCLSID to joy.cpl) when its hardware
    // settings exist; Windows does not create them for HID sticks. Same value as the original Saitek INF.
    if (root == HKEY_LOCAL_MACHINE && RegQueryValueExW(key, L"OEMData", nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
        const BYTE oemData[] = { 0x03, 0x00, 0x08, 0x10, 0x0C, 0x00, 0x00, 0x00 };  // JOYREGHWSETTINGS: flags, 12 buttons
        RegSetValueExW(key, L"OEMData", 0, REG_BINARY, oemData, sizeof(oemData));
    }
    RegCloseKey(key);

    HKEY ff;
    rc = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &ff, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;
    DIFFDEVICEATTRIBUTES attributes = { 0, 1000, 1000 };
    SetString(ff, L"CLSID", GuidString(CLSID_SaitekFF).c_str());
    RegSetValueExW(ff, L"Attributes", 0, REG_BINARY, (const BYTE*)&attributes, sizeof(attributes));
    for (const auto& effect : g_effects) {
        wchar_t sub[128];
        swprintf_s(sub, L"Effects\\%s", effect.guid);
        if (RegCreateKeyExW(ff, sub, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            SetString(key, nullptr, effect.name);
            RegSetValueExW(key, L"Attributes", 0, REG_BINARY, (const BYTE*)&effect.attributes,
                           sizeof(effect.attributes));
            RegCloseKey(key);
        }
    }
    RegCloseKey(ff);
    return ERROR_SUCCESS;
}

bool ValueIs(HKEY root, const wchar_t* path, const wchar_t* name, REFGUID guid)
{
    wchar_t current[64] = L"";
    DWORD size = sizeof(current);
    return RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, current, &size) == ERROR_SUCCESS &&
           !_wcsicmp(current, GuidString(guid).c_str());
}

// Removes our registration for one stick (only the parts that still point to this driver)
void UnregisterDevice(HKEY root, const wchar_t* device)
{
    wchar_t oem[256], path[256];
    swprintf_s(oem, L"%s%s", OemKey, device);
    swprintf_s(path, L"%s\\OEMForceFeedback", oem);
    if (ValueIs(root, path, L"CLSID", CLSID_SaitekFF))
        RegDeleteTreeW(root, path);
    if (ValueIs(root, oem, L"ConfigCLSID", CLSID_SaitekFFConfig))
        RegDeleteKeyValueW(root, oem, L"ConfigCLSID");
}

// In a 32-bit process this lands in the WOW64 view of Software\Classes, as it should
LSTATUS RegisterComClass(HKEY root, REFCLSID clsid, const wchar_t* name)
{
    wchar_t module[MAX_PATH], key[128];
    GetModuleFileNameW(g_module, module, MAX_PATH);
    swprintf_s(key, L"Software\\Classes\\CLSID\\%s", GuidString(clsid).c_str());
    HKEY k;
    LSTATUS rc = RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;
    SetString(k, nullptr, name);
    RegCloseKey(k);
    wcscat_s(key, L"\\InprocServer32");
    rc = RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;
    SetString(k, nullptr, module);
    SetString(k, L"ThreadingModel", L"Both");
    RegCloseKey(k);
    return ERROR_SUCCESS;
}

void UnregisterComClass(HKEY root, REFCLSID clsid)
{
    wchar_t key[128];
    swprintf_s(key, L"Software\\Classes\\CLSID\\%s", GuidString(clsid).c_str());
    RegDeleteTreeW(root, key);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        LogInit();
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* ppv)
{
    if (clsid == CLSID_SaitekFF)
        return g_driverFactory.QueryInterface(riid, ppv);
    if (clsid == CLSID_SaitekFFConfig)
        return g_sheetFactory.QueryInterface(riid, ppv);
    *ppv = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() { return g_objects == 0 ? S_OK : S_FALSE; }

// Registers the COM classes and the sticks' OEM data. The machine-wide variant (needs admin) writes HKLM
// and the current user's copy; the per-user variant writes HKCU only.
static HRESULT Register(bool machine)
{
    HKEY root = machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    LSTATUS rc = RegisterComClass(root, CLSID_SaitekFF, L"Saitek Cyborg force feedback driver");
    if (rc == ERROR_SUCCESS)
        rc = RegisterComClass(root, CLSID_SaitekFFConfig, L"Saitek Cyborg force feedback settings");
    for (const auto& device : g_devices) {
        if (rc == ERROR_SUCCESS)
            rc = RegisterDevice(root, device.hardwareId);
        if (rc == ERROR_SUCCESS && machine)
            RegisterDevice(HKEY_CURRENT_USER, device.hardwareId);
    }
    return HRESULT_FROM_WIN32(rc);
}

static HRESULT Unregister(bool machine)
{
    HKEY root = machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    UnregisterComClass(root, CLSID_SaitekFF);
    UnregisterComClass(root, CLSID_SaitekFFConfig);
    for (const auto& device : g_devices) {
        if (machine)
            UnregisterDevice(HKEY_LOCAL_MACHINE, device.hardwareId);
        UnregisterDevice(HKEY_CURRENT_USER, device.hardwareId);
    }
    return S_OK;
}

STDAPI DllRegisterServer() { return Register(true); }

STDAPI DllUnregisterServer() { return Unregister(true); }

// rundll32 SaitekFF.dll,ApplySettings  sends the saved centering spring settings to the stick
extern "C" void CALLBACK ApplySettingsW(HWND, HINSTANCE, LPWSTR, int)
{
    ApplyCentering(Settings::Load());
}

// regsvr32 /n /i:user SaitekFF.dll  registers for the current user only (no admin rights needed)
STDAPI DllInstall(BOOL install, LPCWSTR cmdLine)
{
    bool machine = !cmdLine || _wcsicmp(cmdLine, L"user") != 0;
    return install ? Register(machine) : Unregister(machine);
}
