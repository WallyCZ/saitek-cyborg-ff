#include "settings.h"
#include "devices.h"
#include "hiddevice.h"
#include "log.h"
#include <setupapi.h>
#include <algorithm>

#pragma comment(lib, "setupapi.lib")

namespace {

constexpr wchar_t Key[] = L"Software\\SaitekFF";

DWORD Read(const wchar_t* name, DWORD fallback, DWORD max)
{
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return fallback;
    return std::min(value, max);
}

void Write(HKEY key, const wchar_t* name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
}

}  // namespace

Settings Settings::Load()
{
    Settings s;
    s.gain = Read(L"Gain", s.gain, 100);
    s.springGain = Read(L"SpringGain", s.springGain, 100);
    s.damperGain = Read(L"DamperGain", s.damperGain, 100);
    s.centering = Read(L"Centering", s.centering, 1) != 0;
    s.centeringStrength = Read(L"CenteringStrength", s.centeringStrength, 100);
    return s;
}

void Settings::Save() const
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    Write(key, L"Gain", gain);
    Write(key, L"SpringGain", springGain);
    Write(key, L"DamperGain", damperGain);
    Write(key, L"Centering", centering);
    Write(key, L"CenteringStrength", centeringStrength);
    RegCloseKey(key);
}

void ApplyCentering(const Settings& settings)
{
    if (HANDLE session = OpenEventW(SYNCHRONIZE, FALSE, SessionEventName)) {
        CloseHandle(session);
        Log("centering not applied now: a force feedback session is running and will apply it when it ends");
        return;
    }
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return;
    SP_DEVICE_INTERFACE_DATA ifd = { sizeof(ifd) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); i++) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
        std::vector<BYTE> buffer(size);
        auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, nullptr))
            continue;
        bool supported = false;
        for (const auto& d : g_devices) {
            wchar_t match[32];
            swprintf_s(match, L"vid_%04x&pid_%04x", d.vendorId, d.productId);
            supported = supported || wcsstr(detail->DevicePath, match) != nullptr;
        }
        if (!supported)
            continue;
        HidDevice device;
        if (!device.Open(detail->DevicePath))
            continue;
        device.SetCentering(settings.CenteringValue());
        Log("centering %s, strength %lu", settings.centering ? "on" : "off", settings.centeringStrength);
    }
    SetupDiDestroyDeviceInfoList(set);
}
