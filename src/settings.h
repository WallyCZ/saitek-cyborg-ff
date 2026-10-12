#pragma once
#include <windows.h>

// User settings from the Force Feedback page in the Game Controllers panel, stored in HKCU\Software\SaitekFF.
// Percentages are 0..100.
struct Settings {
    DWORD gain = 100;               // all effects
    DWORD springGain = 100;         // spring effects
    DWORD damperGain = 100;         // damper, inertia and friction effects
    bool centering = true;          // centering spring when no game uses force feedback
    DWORD centeringStrength = 50;

    // Strength of the stick's centering spring when no game drives it, in device units (0..127)
    int CenteringValue() const { return centering ? (int)centeringStrength * 127 / 100 : 0; }

    static Settings Load();
    void Save() const;
};

// Sends the centering spring settings to every connected stick (used when no game drives the stick)
void ApplyCentering(const Settings& settings);
