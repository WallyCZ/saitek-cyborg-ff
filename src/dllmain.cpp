#include <initguid.h>
#include "effectdriver.h"
#include "proppage.h"
#include "registration.h"
#include "settings.h"
#include "log.h"

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "ole32.lib")

LONG g_objects;
HMODULE g_module;

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

// regsvr32 SaitekFF.dll (as administrator): machine-wide registration plus the current user's DirectInput copy
STDAPI DllRegisterServer()
{
    HRESULT hr = RegisterClasses(HKEY_LOCAL_MACHINE);
    if (SUCCEEDED(hr))
        hr = RegisterDevices(HKEY_LOCAL_MACHINE);
    if (SUCCEEDED(hr))
        RegisterDevices(HKEY_CURRENT_USER);
    return hr;
}

STDAPI DllUnregisterServer()
{
    UnregisterClasses(HKEY_LOCAL_MACHINE);
    UnregisterDevices(HKEY_LOCAL_MACHINE);
    UnregisterDevices(HKEY_CURRENT_USER);
    return S_OK;
}

// regsvr32 /n /i:user SaitekFF.dll  registers for the current user only (no admin rights needed)
STDAPI DllInstall(BOOL install, LPCWSTR cmdLine)
{
    if (!cmdLine || _wcsicmp(cmdLine, L"user") != 0)
        return install ? DllRegisterServer() : DllUnregisterServer();
    if (!install) {
        UnregisterClasses(HKEY_CURRENT_USER);
        UnregisterDevices(HKEY_CURRENT_USER);
        return S_OK;
    }
    HRESULT hr = RegisterClasses(HKEY_CURRENT_USER);
    return SUCCEEDED(hr) ? RegisterDevices(HKEY_CURRENT_USER) : hr;
}

// rundll32 SaitekFF.dll,ApplySettings  sends the saved centering spring settings to the stick
extern "C" void CALLBACK ApplySettingsW(HWND, HINSTANCE, LPWSTR, int)
{
    ApplyCentering(Settings::Load());
}
