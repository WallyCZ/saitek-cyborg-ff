#pragma once
#include <windows.h>

// Sticks driven by this driver. All are Immersion based Saitek sticks; the Cyborg Evo Force is tested,
// the 3D Rumble Force has a different layout (see `rumble`).
struct SupportedDevice {
    USHORT vendorId, productId;
    const wchar_t* hardwareId;   // as used in the DirectInput OEM registry keys
    const wchar_t* renamedName;  // name for games that block the original one because of its crashing driver (DCS)
    // A rumble motor instead of force motors on the axes: its descriptor marks no axis as force feedback capable
    // and it has no spring or damper effects. Registration declares X and Y as force feedback axes, without
    // which DirectInput refuses every effect, and registers only the effects it can play.
    bool rumble;
};

constexpr SupportedDevice g_devices[] = {
    { 0x06A3, 0xFFB5, L"VID_06A3&PID_FFB5", L"Saitek Cyborg Evo FF", false },        // Saitek Cyborg Evo Force
    { 0x06A3, 0xFF12, L"VID_06A3&PID_FF12", L"Saitek Cyborg 3D FS", false },         // Saitek Cyborg 3D Force Stick
    { 0x06A3, 0xFF52, L"VID_06A3&PID_FF52", L"Saitek Cyborg 3D Rumble FF", true },   // Saitek Cyborg 3D Rumble Force
};
