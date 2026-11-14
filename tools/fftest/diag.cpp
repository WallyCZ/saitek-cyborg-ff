// fftest diag - collects what is needed to support a stick remotely, into fftest-diag.txt next to the exe:
// the installed driver, the stick's HID report layout, what DirectInput reports about it, and which effects
// DirectInput accepts on which axes (with the exact error codes).
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

FILE* g_file;

void Out(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    if (g_file) {
        va_start(args, format);
        vfprintf(g_file, format, args);
        va_end(args);
    }
}

const char* DiError(HRESULT hr)
{
    switch (hr) {
    case DI_OK: return "OK";
    case DI_TRUNCATED: return "DI_TRUNCATED";
    case DI_EFFECTRESTARTED: return "DI_EFFECTRESTARTED";
    case DI_TRUNCATEDANDRESTARTED: return "DI_TRUNCATEDANDRESTARTED";
    case DIERR_INVALIDPARAM: return "DIERR_INVALIDPARAM";
    case DIERR_UNSUPPORTED: return "DIERR_UNSUPPORTED";
    case DIERR_DEVICEFULL: return "DIERR_DEVICEFULL";
    case DIERR_NOTACQUIRED: return "DIERR_NOTACQUIRED";
    case DIERR_NOTEXCLUSIVEACQUIRED: return "DIERR_NOTEXCLUSIVEACQUIRED";
    case DIERR_DEVICENOTREG: return "DIERR_DEVICENOTREG (also: COM class not registered)";
    case DIERR_OBJECTNOTFOUND: return "DIERR_OBJECTNOTFOUND";
    case DIERR_INCOMPLETEEFFECT: return "DIERR_INCOMPLETEEFFECT";
    case DIERR_NOTDOWNLOADED: return "DIERR_NOTDOWNLOADED";
    case DIERR_INPUTLOST: return "DIERR_INPUTLOST";
    case DIERR_GENERIC: return "DIERR_GENERIC";
    case DIERR_OTHERAPPHASPRIO: return "DIERR_OTHERAPPHASPRIO";
    case E_NOINTERFACE: return "E_NOINTERFACE";
    }
    return "";
}

// --------------------------------------------------------------------------------------------------------------
// Installed driver

void RegistryValue(HKEY root, const wchar_t* key, const wchar_t* name, const char* label)
{
    wchar_t value[512];
    DWORD size = sizeof(value);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS)
        Out("  %s: %ls\n", label, value);
    else
        Out("  %s: (none)\n", label);
}

void FileVersion(const wchar_t* path)
{
    DWORD handle, size = GetFileVersionInfoSizeW(path, &handle);
    std::vector<BYTE> data(size ? size : 1);
    VS_FIXEDFILEINFO* info;
    UINT len;
    if (size && GetFileVersionInfoW(path, 0, size, data.data()) && VerQueryValueW(data.data(), L"\\", (void**)&info, &len))
        Out("    file version %u.%u.%u.%u\n", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
            HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    else
        Out("    file version: none (%ls)\n", GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES ? L"file missing" : L"no version resource");
}

void InstalledDriver(USHORT vendor, USHORT product)
{
    wchar_t oem[256];
    swprintf_s(oem, L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\VID_%04X&PID_%04X",
               vendor, product);
    for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
        Out(" %s OEM key:\n", root == HKEY_LOCAL_MACHINE ? "HKLM" : "HKCU");
        RegistryValue(root, oem, L"OEMName", "OEMName");
        RegistryValue(root, oem, L"ConfigCLSID", "ConfigCLSID");
        std::wstring ff = std::wstring(oem) + L"\\OEMForceFeedback";
        RegistryValue(root, ff.c_str(), L"CLSID", "OEMForceFeedback CLSID");
        HKEY effects;
        if (RegOpenKeyExW(root, (ff + L"\\Effects").c_str(), 0, KEY_READ, &effects) == ERROR_SUCCESS) {
            DWORD count = 0;
            RegQueryInfoKeyW(effects, nullptr, nullptr, nullptr, &count, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
            Out("  registered effects: %lu\n", count);
            RegCloseKey(effects);
        }
        wchar_t clsid[64];
        DWORD size = sizeof(clsid);
        if (RegGetValueW(root, ff.c_str(), L"CLSID", RRF_RT_REG_SZ, nullptr, clsid, &size) == ERROR_SUCCESS) {
            std::wstring server = std::wstring(L"CLSID\\") + clsid + L"\\InprocServer32";
            wchar_t path[MAX_PATH];
            size = sizeof(path);
            if (RegGetValueW(HKEY_CLASSES_ROOT, server.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS) {
                Out("  driver DLL: %ls\n", path);
                FileVersion(path);
            }
        }
    }
}

// --------------------------------------------------------------------------------------------------------------
// HID report layout

const char* TypeName(HIDP_REPORT_TYPE t) { return t == HidP_Input ? "Input" : t == HidP_Output ? "Output" : "Feature"; }

void HidLayout(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Out("  cannot open (%lu)\n", GetLastError());
        return;
    }
    HIDD_ATTRIBUTES attr = { sizeof(attr) };
    HidD_GetAttributes(h, &attr);
    PHIDP_PREPARSED_DATA pp;
    HIDP_CAPS caps;
    if (!HidD_GetPreparsedData(h, &pp) || HidP_GetCaps(pp, &caps) != HIDP_STATUS_SUCCESS) {
        Out("  cannot read capabilities\n");
        CloseHandle(h);
        return;
    }
    Out("  VID %04X PID %04X version %04X, top usage %02X:%02X\n", attr.VendorID, attr.ProductID, attr.VersionNumber,
        caps.UsagePage, caps.Usage);
    Out("  report lengths: input %u, output %u, feature %u\n", caps.InputReportByteLength, caps.OutputReportByteLength,
        caps.FeatureReportByteLength);

    ULONG nodes = caps.NumberLinkCollectionNodes;
    std::vector<HIDP_LINK_COLLECTION_NODE> links(nodes);
    if (nodes && HidP_GetLinkCollectionNodes(links.data(), &nodes, pp) == HIDP_STATUS_SUCCESS) {
        Out("  collections:\n");
        for (ULONG i = 0; i < nodes; i++)
            Out("    %2lu: %02X:%02X type %u parent %u\n", i, links[i].LinkUsagePage, links[i].LinkUsage,
                links[i].CollectionType, links[i].Parent);
    }

    std::vector<UCHAR> featureIds;
    for (HIDP_REPORT_TYPE type : { HidP_Input, HidP_Output, HidP_Feature }) {
        USHORT nb = type == HidP_Input ? caps.NumberInputButtonCaps : type == HidP_Output ? caps.NumberOutputButtonCaps : caps.NumberFeatureButtonCaps;
        USHORT nv = type == HidP_Input ? caps.NumberInputValueCaps : type == HidP_Output ? caps.NumberOutputValueCaps : caps.NumberFeatureValueCaps;
        std::vector<HIDP_BUTTON_CAPS> buttons(nb ? nb : 1);
        std::vector<HIDP_VALUE_CAPS> values(nv ? nv : 1);
        if (nb)
            HidP_GetButtonCaps(type, buttons.data(), &nb, pp);
        if (nv)
            HidP_GetValueCaps(type, values.data(), &nv, pp);
        for (USHORT i = 0; i < nb; i++) {
            const auto& b = buttons[i];
            Out("  %-7s id %02X button %02X:%02X-%02X  collection %u\n", TypeName(type), b.ReportID, b.UsagePage,
                b.IsRange ? b.Range.UsageMin : b.NotRange.Usage, b.IsRange ? b.Range.UsageMax : b.NotRange.Usage, b.LinkCollection);
            if (type == HidP_Feature)
                featureIds.push_back(b.ReportID);
        }
        for (USHORT i = 0; i < nv; i++) {
            const auto& v = values[i];
            Out("  %-7s id %02X value  %02X:%02X%s  collection %u  %u bits x %u  logical %ld..%ld  physical %ld..%ld\n",
                TypeName(type), v.ReportID, v.UsagePage, v.IsRange ? v.Range.UsageMin : v.NotRange.Usage,
                v.IsRange ? "+" : "", v.LinkCollection, v.BitSize, v.ReportCount, v.LogicalMin, v.LogicalMax,
                v.PhysicalMin, v.PhysicalMax);
            if (type == HidP_Feature)
                featureIds.push_back(v.ReportID);
        }
    }
    for (size_t i = 0; i < featureIds.size(); i++) {
        if (i && featureIds[i] == featureIds[i - 1])
            continue;
        std::vector<BYTE> report(caps.FeatureReportByteLength, 0);
        report[0] = featureIds[i];
        Out("  feature report %02X:", featureIds[i]);
        if (HidD_GetFeature(h, report.data(), (ULONG)report.size()))
            for (BYTE b : report)
                Out(" %02X", b);
        else
            Out(" cannot read (%lu)", GetLastError());
        Out("\n");
    }
    HidD_FreePreparsedData(pp);
    CloseHandle(h);
}

void HidDevices()
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA ifd = { sizeof(ifd) };
    int found = 0;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); i++) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
        std::vector<BYTE> buf(size);
        auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, nullptr) || !wcsstr(detail->DevicePath, L"vid_06a3"))
            continue;
        Out("\nHID device %ls\n", detail->DevicePath);
        HidLayout(detail->DevicePath);
        found++;
    }
    SetupDiDestroyDeviceInfoList(set);
    if (!found)
        Out("\nNo Saitek HID device (VID 06A3) found. Is the stick connected?\n");
}

// --------------------------------------------------------------------------------------------------------------
// DirectInput

struct Stick {
    GUID instance;
    DWORD product;
    wchar_t name[MAX_PATH];
};

BOOL CALLBACK CollectStick(LPCDIDEVICEINSTANCEW inst, LPVOID context)
{
    if (LOWORD(inst->guidProduct.Data1) == 0x06A3) {
        Stick s = { inst->guidInstance, inst->guidProduct.Data1 };
        wcscpy_s(s.name, inst->tszProductName);
        static_cast<std::vector<Stick>*>(context)->push_back(s);
    }
    return DIENUM_CONTINUE;
}

struct ObjectList {
    std::vector<DWORD> ffAxes;  // offsets of axes DirectInput marks as force feedback actuators
};

BOOL CALLBACK ShowObject(LPCDIDEVICEOBJECTINSTANCEW o, LPVOID context)
{
    Out("    %-24ls offset %3lu type %08lX flags %08lX%s%s  ffMaxForce %lu\n", o->tszName, o->dwOfs, o->dwType,
        o->dwFlags, (o->dwFlags & DIDOI_FFACTUATOR) ? " FF-ACTUATOR" : "", (o->dwFlags & DIDOI_FFEFFECTTRIGGER) ? " FF-TRIGGER" : "",
        o->dwFFMaxForce);
    if ((o->dwType & DIDFT_AXIS) && (o->dwFlags & DIDOI_FFACTUATOR))
        static_cast<ObjectList*>(context)->ffAxes.push_back(o->dwOfs);
    return DIENUM_CONTINUE;
}

BOOL CALLBACK ShowEffect(LPCDIEFFECTINFOW e, LPVOID)
{
    Out("    %-24ls type %08lX static %08lX dynamic %08lX\n", e->tszName, e->dwEffType, e->dwStaticParams, e->dwDynamicParams);
    return DIENUM_CONTINUE;
}

void TryEffect(IDirectInputDevice8W* dev, const char* name, const GUID& type, const std::vector<DWORD>& axes, const char* axesName)
{
    DICONSTANTFORCE constant = { 5000 };
    DIPERIODIC periodic = { 5000, 0, 0, 100 * 1000 };
    DICONDITION condition[2] = { { 0, 5000, 5000, 10000, 10000, 0 }, { 0, 5000, 5000, 10000, 10000, 0 } };
    bool isCondition = type == GUID_Spring || type == GUID_Damper;
    std::vector<DWORD> offsets = axes;
    std::vector<LONG> direction(axes.size(), 0);
    if (!direction.empty())
        direction[0] = 1;
    DIEFFECT e = { sizeof(e) };
    e.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.dwDuration = 500 * 1000;
    e.dwGain = DI_FFNOMINALMAX;
    e.dwTriggerButton = DIEB_NOTRIGGER;
    e.cAxes = (DWORD)offsets.size();
    e.rgdwAxes = offsets.data();
    e.rglDirection = direction.data();
    if (type == GUID_ConstantForce) {
        e.cbTypeSpecificParams = sizeof(constant);
        e.lpvTypeSpecificParams = &constant;
    } else if (isCondition) {
        e.cbTypeSpecificParams = (DWORD)(sizeof(DICONDITION) * axes.size());
        e.lpvTypeSpecificParams = condition;
    } else {
        e.cbTypeSpecificParams = sizeof(periodic);
        e.lpvTypeSpecificParams = &periodic;
    }
    IDirectInputEffect* effect = nullptr;
    HRESULT hr = dev->CreateEffect(type, &e, &effect, nullptr);  // downloads, does not play
    Out("    %-9s on %-12s -> %08lX %s\n", name, axesName, hr, DiError(hr));
    if (effect)
        effect->Release();
}

void DirectInputInfo()
{
    IDirectInput8W* di;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, nullptr))) {
        Out("\nDirectInput not available\n");
        return;
    }
    std::vector<Stick> sticks;
    di->EnumDevices(DI8DEVCLASS_GAMECTRL, CollectStick, &sticks, DIEDFL_ATTACHEDONLY);
    if (sticks.empty())
        Out("\nDirectInput: no Saitek game controller attached\n");
    HWND window = CreateWindowExW(0, L"STATIC", L"fftest diag", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    for (const auto& s : sticks) {
        Out("\nDirectInput device: %ls (product %08lX)\n", s.name, s.product);
        IDirectInputDevice8W* dev;
        if (FAILED(di->CreateDevice(s.instance, &dev, nullptr)))
            continue;
        DIDEVCAPS caps = { sizeof(caps) };
        dev->GetCapabilities(&caps);
        Out("  flags %08lX%s, axes %lu, buttons %lu, POVs %lu, FF sample period %lu, min time resolution %lu, FF driver version %08lX\n",
            caps.dwFlags, (caps.dwFlags & DIDC_FORCEFEEDBACK) ? " FORCEFEEDBACK" : "", caps.dwAxes, caps.dwButtons, caps.dwPOVs,
            caps.dwFFSamplePeriod, caps.dwFFMinTimeResolution, caps.dwFFDriverVersion);
        ObjectList objects;
        Out("  objects:\n");
        dev->EnumObjects(ShowObject, &objects, DIDFT_ALL);
        Out("  effects:\n");
        dev->EnumEffects(ShowEffect, nullptr, DIEFT_ALL);

        if (caps.dwFlags & DIDC_FORCEFEEDBACK) {
            HRESULT hr = dev->SetDataFormat(&c_dfDIJoystick);
            if (SUCCEEDED(hr))
                hr = dev->SetCooperativeLevel(window, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
            if (SUCCEEDED(hr))
                hr = dev->Acquire();
            Out("  exclusive acquire -> %08lX %s\n", hr, DiError(hr));
            if (SUCCEEDED(hr)) {
                Out("  effect creation (downloaded to the stick, not played):\n");
                struct { const char* name; const GUID* type; } types[] = {
                    { "constant", &GUID_ConstantForce }, { "sine", &GUID_Sine }, { "square", &GUID_Square },
                    { "spring", &GUID_Spring }, { "damper", &GUID_Damper },
                };
                struct { const char* name; std::vector<DWORD> axes; } axisSets[] = {
                    { "X", { DIJOFS_X } }, { "Y", { DIJOFS_Y } }, { "X+Y", { DIJOFS_X, DIJOFS_Y } },
                    { "FF axes", objects.ffAxes },
                };
                for (auto& t : types)
                    for (auto& a : axisSets)
                        if (!a.axes.empty() && a.axes.size() <= 2)
                            TryEffect(dev, t.name, *t.type, a.axes, a.name);
                dev->Unacquire();
            }
        }
        dev->Release();
    }
    DestroyWindow(window);
    di->Release();
}

}  // namespace

int Diagnose()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring file = path;
    file = file.substr(0, file.find_last_of(L'\\') + 1) + L"fftest-diag.txt";
    if (_wfopen_s(&g_file, file.c_str(), L"w") != 0)
        g_file = nullptr;

    OSVERSIONINFOEXW os = { sizeof(os) };
    auto rtlGetVersion = (LONG(WINAPI*)(OSVERSIONINFOEXW*))GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (rtlGetVersion)
        rtlGetVersion(&os);
    SYSTEMTIME t;
    GetLocalTime(&t);
    Out("Saitek force feedback diagnostics (fftest diag), %04d-%02d-%02d %02d:%02d, Windows %lu.%lu build %lu\n", t.wYear,
        t.wMonth, t.wDay, t.wHour, t.wMinute, os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);

    for (USHORT product : { 0xFFB5, 0xFF12, 0xFF52 }) {
        Out("\nRegistration for VID_06A3&PID_%04X\n", product);
        InstalledDriver(0x06A3, product);
    }
    HidDevices();
    DirectInputInfo();

    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
        printf("\nSaved to %ls\n", file.c_str());
    }
    return 0;
}
