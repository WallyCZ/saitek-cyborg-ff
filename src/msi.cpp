// Custom actions for the MSI installer (installer\SaitekFF.wxs). They run from the 64-bit DLL.
#include "registration.h"
#include "settings.h"
#include "log.h"
#include <msi.h>
#include <msiquery.h>

#pragma comment(lib, "msi.lib")

// Immediate, before the setup dialog: sets SAITEKDRIVERFOUND when the original Saitek driver is installed,
// so the dialog can offer to remove it
extern "C" UINT __stdcall MsiDetectSaitekDriver(MSIHANDLE install)
{
    if (!FindSaitekDriver().empty())
        MsiSetPropertyW(install, L"SAITEKDRIVERFOUND", L"1");
    return ERROR_SUCCESS;
}

// Deferred, as the local system account: machine-wide registration. CustomActionData is "1" when the user
// chose to remove the original Saitek driver.
extern "C" UINT __stdcall MsiRegisterMachine(MSIHANDLE install)
{
    wchar_t data[16] = L"";
    DWORD size = ARRAYSIZE(data);
    MsiGetPropertyW(install, L"CustomActionData", data, &size);
    if (!wcscmp(data, L"1"))
        RemoveSaitekDriver();
    HRESULT hr = RegisterClasses(HKEY_LOCAL_MACHINE);
    if (SUCCEEDED(hr))
        hr = RegisterDevices(HKEY_LOCAL_MACHINE);
    Log("MSI machine registration: %08lX", hr);
    return SUCCEEDED(hr) ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
}

// Deferred (and rollback), as the local system account
extern "C" UINT __stdcall MsiUnregisterMachine(MSIHANDLE)
{
    UnregisterClasses(HKEY_LOCAL_MACHINE);
    UnregisterDevices(HKEY_LOCAL_MACHINE);
    return ERROR_SUCCESS;
}

// Deferred, impersonating the installing user: their DirectInput copy of the stick's data, the device name
// (CustomActionData "1" when the user chose to rename the stick) and the centering spring setting.
// Failures here do not fail the installation.
extern "C" UINT __stdcall MsiRegisterUser(MSIHANDLE install)
{
    HRESULT hr = RegisterDevices(HKEY_CURRENT_USER);
    Log("MSI user registration: %08lX", hr);
    wchar_t data[16] = L"";
    DWORD size = ARRAYSIZE(data);
    MsiGetPropertyW(install, L"CustomActionData", data, &size);
    if (!wcscmp(data, L"1"))
        RenameDevices();
    ApplyCentering(Settings::Load());
    return ERROR_SUCCESS;
}

// Deferred (and rollback), impersonating the installing user
extern "C" UINT __stdcall MsiUnregisterUser(MSIHANDLE)
{
    UnregisterDevices(HKEY_CURRENT_USER);
    return ERROR_SUCCESS;
}
