#include "log.h"
#include <stdio.h>
#include <stdarg.h>

static bool g_enabled;
static wchar_t g_logPath[MAX_PATH];
static CRITICAL_SECTION g_lock;

void LogInit()
{
    InitializeCriticalSection(&g_lock);
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\SaitekFF", L"Debug", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        g_enabled = value != 0;
    if (g_enabled) {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        swprintf_s(g_logPath, L"%sSaitekFF.log", temp);
    }
}

void Log(const char* format, ...)
{
    if (!g_enabled)
        return;
    char line[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = sprintf_s(line, "SaitekFF [%5lu] %02d:%02d:%02d.%03d ", GetCurrentProcessId(), t.wHour, t.wMinute, t.wSecond,
                      t.wMilliseconds);
    va_list args;
    va_start(args, format);
    vsnprintf(line + n, sizeof(line) - n - 2, format, args);
    va_end(args);
    strcat_s(line, "\n");
    OutputDebugStringA(line);

    EnterCriticalSection(&g_lock);
    FILE* f;
    if (_wfopen_s(&f, g_logPath, L"a") == 0) {
        fputs(line, f);
        fclose(f);
    }
    LeaveCriticalSection(&g_lock);
}
