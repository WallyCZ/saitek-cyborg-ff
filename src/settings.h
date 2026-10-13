#pragma once
#include <windows.h>

// User settings from the Force Feedback page in the Game Controllers panel, stored in HKCU\Software\SaitekFF.
// Percentages are 0..100.
struct Settings {
    DWORD gain = 100;               // all effects
    DWORD springGain = 100;         // spring effects
    DWORD damperGain = 100;         // damper, inertia and friction effects
    bool centering = true;          // centering spring when no game uses force feedback
    DWORD centeringStrength = 65;

    // Strength of the stick's centering spring when no game drives it, in device units (0..127)
    int CenteringValue() const { return centering ? (int)centeringStrength * 127 / 100 : 0; }

    static Settings Load();
    void Save() const;
};

// Exists while a game (or the page's test effects) drives the stick; the driver keeps the stick's own
// centering spring off then and restores it when the session ends
constexpr wchar_t SessionEventName[] = L"Local\\SaitekFF.ForceFeedbackSession";

// Sends the centering spring settings to every connected stick, unless a force feedback session runs
void ApplyCentering(const Settings& settings);
