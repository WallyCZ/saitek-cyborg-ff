#pragma once
#include <windows.h>

// Sticks driven by this driver. All are Immersion based Saitek sticks with the same report layout;
// only the Cyborg Evo Force has been tested.
struct SupportedDevice {
    USHORT vendorId, productId;
    const wchar_t* hardwareId;  // as used in the DirectInput OEM registry keys
};

constexpr SupportedDevice g_devices[] = {
    { 0x06A3, 0xFFB5, L"VID_06A3&PID_FFB5" },  // Saitek Cyborg Evo Force
    { 0x06A3, 0xFF12, L"VID_06A3&PID_FF12" },  // Saitek Cyborg 3D Force Stick
    { 0x06A3, 0xFF52, L"VID_06A3&PID_FF52" },  // Saitek Cyborg 3D Rumble Force
};
