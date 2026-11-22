#pragma once
#include <windows.h>

// Diagnostic logging, off by default. Enable with
//   HKCU\Software\SaitekFF  Debug (DWORD) = 1
// Messages go to OutputDebugString (view with DebugView) and to %TEMP%\SaitekFF.log.
void LogInit();
void Log(const char* format, ...);
bool LogEnabled();  // to skip building expensive messages
// Logs a report as hex, e.g. "sent 0D: 0D 01 7F"
void LogBytes(const char* prefix, const unsigned char* data, size_t size);
