#include <initguid.h>
#include "effectdriver.h"
#include "log.h"
#include <stdio.h>

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

LONG g_objects;
static HMODULE g_module;

namespace {

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
        auto driver = new (std::nothrow) EffectDriver();
        if (!driver)
            return E_OUTOFMEMORY;
        HRESULT hr = driver->QueryInterface(riid, ppv);
        driver->Release();
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
};

ClassFactory g_factory;

// Sticks driven by this driver. All are Immersion based Saitek sticks with the same report layout;
// only the Cyborg Evo Force has been tested.
const wchar_t* const g_devices[] = {
    L"VID_06A3&PID_FFB5",  // Saitek Cyborg Evo Force
    L"VID_06A3&PID_FF12",  // Saitek Cyborg 3D Force Stick
    L"VID_06A3&PID_FF52",  // Saitek Cyborg 3D Rumble Force
};

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

void ClsidString(wchar_t* out, size_t size)
{
    StringFromGUID2(CLSID_SaitekFF, out, (int)size);
}

// Writes OEMForceFeedback for one stick into HKLM or HKCU. DirectInput keeps a per-user copy of the OEM
// data in HKCU, which Windows fills with its generic PID driver when the stick is first plugged in.
LSTATUS RegisterDevice(HKEY root, const wchar_t* device)
{
    wchar_t path[256], clsid[64];
    swprintf_s(path, L"%s%s\\OEMForceFeedback", OemKey, device);
    ClsidString(clsid, ARRAYSIZE(clsid));
    RegDeleteTreeW(root, path);

    HKEY ff;
    LSTATUS rc = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &ff, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;
    DIFFDEVICEATTRIBUTES attributes = { 0, 1000, 1000 };
    SetString(ff, L"CLSID", clsid);
    RegSetValueExW(ff, L"Attributes", 0, REG_BINARY, (const BYTE*)&attributes, sizeof(attributes));
    for (const auto& effect : g_effects) {
        HKEY key;
        wchar_t sub[128];
        swprintf_s(sub, L"Effects\\%s", effect.guid);
        if (RegCreateKeyExW(ff, sub, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            SetString(key, nullptr, effect.name);
            RegSetValueExW(key, L"Attributes", 0, REG_BINARY, (const BYTE*)&effect.attributes,
                           sizeof(effect.attributes));
            RegCloseKey(key);
        }
    }
    RegCloseKey(ff);
    return ERROR_SUCCESS;
}

// Removes our OEMForceFeedback registration (only if it still points to this driver)
void UnregisterDevice(HKEY root, const wchar_t* device)
{
    wchar_t path[256], clsid[64], current[64] = L"";
    swprintf_s(path, L"%s%s\\OEMForceFeedback", OemKey, device);
    ClsidString(clsid, ARRAYSIZE(clsid));
    DWORD size = sizeof(current);
    if (RegGetValueW(root, path, L"CLSID", RRF_RT_REG_SZ, nullptr, current, &size) == ERROR_SUCCESS &&
        !_wcsicmp(current, clsid))
        RegDeleteTreeW(root, path);
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
    if (clsid != CLSID_SaitekFF) {
        *ppv = nullptr;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_objects == 0 ? S_OK : S_FALSE; }

// Registers the COM class and the stick's OEMForceFeedback data. The machine-wide variant (needs admin)
// writes HKLM and the current user's copy; the per-user variant writes HKCU only.
static HRESULT Register(bool machine)
{
    wchar_t clsid[64], path[MAX_PATH], key[128];
    ClsidString(clsid, ARRAYSIZE(clsid));
    GetModuleFileNameW(g_module, path, MAX_PATH);
    HKEY root = machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    // In a 32-bit process this lands in the WOW64 view of Software\Classes, as it should
    swprintf_s(key, L"Software\\Classes\\CLSID\\%s", clsid);
    HKEY k;
    LSTATUS rc = RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &k, nullptr);
    if (rc != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(rc);
    SetString(k, nullptr, L"Saitek Cyborg force feedback driver");
    RegCloseKey(k);
    wcscat_s(key, L"\\InprocServer32");
    rc = RegCreateKeyExW(root, key, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &k, nullptr);
    if (rc != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(rc);
    SetString(k, nullptr, path);
    SetString(k, L"ThreadingModel", L"Both");
    RegCloseKey(k);

    for (const wchar_t* device : g_devices) {
        rc = RegisterDevice(root, device);
        if (rc != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(rc);
        if (machine)
            RegisterDevice(HKEY_CURRENT_USER, device);
    }
    return S_OK;
}

static HRESULT Unregister(bool machine)
{
    wchar_t clsid[64], key[128];
    ClsidString(clsid, ARRAYSIZE(clsid));
    swprintf_s(key, L"Software\\Classes\\CLSID\\%s", clsid);
    RegDeleteTreeW(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, key);
    for (const wchar_t* device : g_devices) {
        if (machine)
            UnregisterDevice(HKEY_LOCAL_MACHINE, device);
        UnregisterDevice(HKEY_CURRENT_USER, device);
    }
    return S_OK;
}

STDAPI DllRegisterServer() { return Register(true); }

STDAPI DllUnregisterServer() { return Unregister(true); }

// regsvr32 /n /i:user SaitekFF.dll  registers for the current user only (no admin rights needed)
STDAPI DllInstall(BOOL install, LPCWSTR cmdLine)
{
    bool machine = !cmdLine || _wcsicmp(cmdLine, L"user") != 0;
    return install ? Register(machine) : Unregister(machine);
}
