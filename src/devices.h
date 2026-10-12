#pragma once
#include <windows.h>

// Sticks driven by this driver. All are Immersion based Saitek sticks with the same report layout;
// only the Cyborg Evo Force has been tested.
struct SupportedDevice {
    USHORT vendorId, productId;
    const wchar_t* hardwareId;   // as used in the DirectInput OEM registry keys
    const wchar_t* renamedName;  // name for games that block the original one because of its crashing driver (DCS)
};

constexpr SupportedDevice g_devices[] = {
    { 0x06A3, 0xFFB5, L"VID_06A3&PID_FFB5", L"Saitek Cyborg Evo FF" },        // Saitek Cyborg Evo Force
    { 0x06A3, 0xFF12, L"VID_06A3&PID_FF12", L"Saitek Cyborg 3D FS" },         // Saitek Cyborg 3D Force Stick
    { 0x06A3, 0xFF52, L"VID_06A3&PID_FF52", L"Saitek Cyborg 3D Rumble FF" },  // Saitek Cyborg 3D Rumble Force
};
