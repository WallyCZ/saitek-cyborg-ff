#pragma once
#include <windows.h>

// Diagnostic logging, off by default. Enable with
//   HKCU\Software\SaitekFF  Debug (DWORD) = 1
// Messages go to OutputDebugString (view with DebugView) and to %TEMP%\SaitekFF.log.
void LogInit();
void Log(const char* format, ...);
