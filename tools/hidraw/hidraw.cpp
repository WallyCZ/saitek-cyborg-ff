// hidraw - raw HID report access for protocol experiments
//   hidraw read [count]          print incoming input reports (raw bytes)
//   hidraw write  xx xx ...      send an output report via WriteFile (padded to report length)
//   hidraw output xx xx ...      send an output report via HidD_SetOutputReport (control pipe)
//   hidraw feature id            HidD_GetFeature for report id
// The device is the first HID interface with VID 06A3 / PID FFB5.
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

static HANDLE OpenStick(HIDP_CAPS& caps)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA ifd = { sizeof(ifd) };
    HANDLE result = INVALID_HANDLE_VALUE;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifd); i++) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
        std::vector<BYTE> buf(size);
        auto detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buf.data();
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, nullptr))
            continue;
        if (!wcsstr(_wcsupr(detail->DevicePath), L"VID_06A3&PID_FFB5"))
            continue;
        HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        PHIDP_PREPARSED_DATA pp;
        HidD_GetPreparsedData(h, &pp);
        HidP_GetCaps(pp, &caps);
        HidD_FreePreparsedData(pp);
        result = h;
        break;
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

static void Dump(const char* prefix, const BYTE* data, DWORD len)
{
    printf("%s", prefix);
    for (DWORD i = 0; i < len; i++)
        printf(" %02X", data[i]);
    printf("\n");
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { printf("usage: hidraw read|write|output|feature ...\n"); return 1; }
    HIDP_CAPS caps;
    HANDLE h = OpenStick(caps);
    if (h == INVALID_HANDLE_VALUE) { printf("stick not found\n"); return 1; }
    printf("input %u, output %u, feature %u bytes\n", caps.InputReportByteLength,
           caps.OutputReportByteLength, caps.FeatureReportByteLength);

    std::vector<BYTE> buf(max(max(caps.InputReportByteLength, caps.OutputReportByteLength), caps.FeatureReportByteLength) + 1);
    if (!strcmp(argv[1], "read")) {
        int count = argc > 2 ? atoi(argv[2]) : 20;
        for (int i = 0; i < count; i++) {
            DWORD got = 0;
            if (!ReadFile(h, buf.data(), caps.InputReportByteLength, &got, nullptr)) {
                printf("ReadFile failed %lu\n", GetLastError());
                break;
            }
            Dump("in:", buf.data(), got);
        }
    } else if (!strcmp(argv[1], "write") || !strcmp(argv[1], "output")) {
        std::fill(buf.begin(), buf.end(), 0);
        int n = 0;
        for (int i = 2; i < argc && n < (int)buf.size(); i++)
            buf[n++] = (BYTE)strtoul(argv[i], nullptr, 16);
        Dump("out:", buf.data(), caps.OutputReportByteLength);
        BOOL ok;
        if (!strcmp(argv[1], "write")) {
            DWORD written = 0;
            ok = WriteFile(h, buf.data(), caps.OutputReportByteLength, &written, nullptr);
        } else {
            ok = HidD_SetOutputReport(h, buf.data(), caps.OutputReportByteLength);
        }
        printf("%s %s (error %lu)\n", argv[1], ok ? "OK" : "FAILED", ok ? 0 : GetLastError());
    } else if (!strcmp(argv[1], "feature")) {
        std::fill(buf.begin(), buf.end(), 0);
        buf[0] = (BYTE)strtoul(argv[2], nullptr, 16);
        if (HidD_GetFeature(h, buf.data(), caps.FeatureReportByteLength))
            Dump("feature:", buf.data(), caps.FeatureReportByteLength);
        else
            printf("GetFeature failed %lu\n", GetLastError());
    }
    CloseHandle(h);
    return 0;
}
