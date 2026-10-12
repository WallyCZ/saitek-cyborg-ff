#include "hiddevice.h"
#include "log.h"

#pragma comment(lib, "hid.lib")

OutputReport::OutputReport(const HidDevice& device, UCHAR reportId)
    : device_(device), data_(device.Caps().OutputReportByteLength)
{
    if (!reportId || HidP_InitializeReportForID(HidP_Output, reportId, device.Preparsed(), (PCHAR)data_.data(),
                                                (ULONG)data_.size()) != HIDP_STATUS_SUCCESS)
        ok_ = false;
}

OutputReport& OutputReport::Value(USAGE page, USAGE usage, ULONG raw)
{
    NTSTATUS status = HidP_SetUsageValue(HidP_Output, page, 0, usage, raw, device_.Preparsed(), (PCHAR)data_.data(),
                                         (ULONG)data_.size());
    if (status != HIDP_STATUS_SUCCESS) {
        Log("report %02X: cannot set value %02X:%02X = %lu (0x%08lX)", data_[0], page, usage, raw, status);
        ok_ = false;
    }
    return *this;
}

OutputReport& OutputReport::Scaled(USAGE page, USAGE usage, LONG physical)
{
    NTSTATUS status = HidP_SetScaledUsageValue(HidP_Output, page, 0, usage, physical, device_.Preparsed(),
                                               (PCHAR)data_.data(), (ULONG)data_.size());
    if (status != HIDP_STATUS_SUCCESS) {
        Log("report %02X: cannot set scaled %02X:%02X = %ld (0x%08lX)", data_[0], page, usage, physical, status);
        ok_ = false;
    }
    return *this;
}

OutputReport& OutputReport::Usage(USAGE page, USAGE usage)
{
    ULONG count = 1;
    NTSTATUS status = HidP_SetUsages(HidP_Output, page, 0, &usage, &count, device_.Preparsed(), (PCHAR)data_.data(),
                                     (ULONG)data_.size());
    if (status != HIDP_STATUS_SUCCESS) {
        Log("report %02X: cannot set usage %02X:%02X (0x%08lX)", data_[0], page, usage, status);
        ok_ = false;
    }
    return *this;
}

bool HidDevice::Open(const wchar_t* path)
{
    Close();
    writeHandle_ = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
    readHandle_ = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (writeHandle_ == INVALID_HANDLE_VALUE || readHandle_ == INVALID_HANDLE_VALUE) {
        Log("cannot open %ls (%lu)", path, GetLastError());
        Close();
        return false;
    }
    if (!HidD_GetPreparsedData(writeHandle_, &preparsed_) || HidP_GetCaps(preparsed_, &caps_) != HIDP_STATUS_SUCCESS) {
        Log("cannot read HID capabilities");
        Close();
        return false;
    }
    HidD_GetAttributes(writeHandle_, &attributes_);

    ids.setEffect = FindReportId(HidP_Output, usage::PagePid, usage::Duration);
    ids.envelope = FindReportId(HidP_Output, usage::PagePid, usage::AttackTime);
    ids.condition = FindReportId(HidP_Output, usage::PagePid, usage::PositiveCoefficient);
    ids.periodic = FindReportId(HidP_Output, usage::PagePid, usage::Period);
    ids.constant = FindReportId(HidP_Output, usage::PagePid, usage::Magnitude, ids.periodic);
    ids.effectOperation = FindReportId(HidP_Output, usage::PagePid, usage::LoopCount);
    ids.deviceControl = FindReportId(HidP_Output, usage::PagePid, usage::DeviceReset);
    ids.deviceGain = FindReportId(HidP_Output, usage::PagePid, usage::DeviceGain);
    ids.vendor = FindReportId(HidP_Output, usage::PageVendor, usage::VendorValue);
    ids.pool = FindReportId(HidP_Feature, usage::PagePid, usage::RamPoolSize);
    ids.state = FindReportId(HidP_Input, usage::PagePid, usage::EffectPlaying);
    Log("opened %04X:%04X v%04X, reports: effect %02X env %02X cond %02X per %02X const %02X op %02X ctl %02X "
        "gain %02X vendor %02X pool %02X state %02X",
        attributes_.VendorID, attributes_.ProductID, attributes_.VersionNumber, ids.setEffect, ids.envelope,
        ids.condition, ids.periodic, ids.constant, ids.effectOperation, ids.deviceControl, ids.deviceGain, ids.vendor,
        ids.pool, ids.state);
    if (!ids.setEffect || !ids.effectOperation || !ids.deviceControl) {
        Log("device has no usable force feedback reports");
        Close();
        return false;
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, ReaderThread, this, 0, nullptr);
    return true;
}

void HidDevice::Close()
{
    if (thread_) {
        SetEvent(stopEvent_);
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (readHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(readHandle_);
        readHandle_ = INVALID_HANDLE_VALUE;
    }
    if (writeHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(writeHandle_);
        writeHandle_ = INVALID_HANDLE_VALUE;
    }
    if (preparsed_) {
        HidD_FreePreparsedData(preparsed_);
        preparsed_ = nullptr;
    }
}

UCHAR HidDevice::FindReportId(HIDP_REPORT_TYPE type, USAGE page, USAGE usage, UCHAR exclude) const
{
    USHORT count = type == HidP_Input ? caps_.NumberInputValueCaps
                 : type == HidP_Output ? caps_.NumberOutputValueCaps : caps_.NumberFeatureValueCaps;
    if (count) {
        std::vector<HIDP_VALUE_CAPS> values(count);
        if (HidP_GetValueCaps(type, values.data(), &count, preparsed_) == HIDP_STATUS_SUCCESS)
            for (USHORT i = 0; i < count; i++) {
                const auto& v = values[i];
                USAGE first = v.IsRange ? v.Range.UsageMin : v.NotRange.Usage;
                USAGE last = v.IsRange ? v.Range.UsageMax : v.NotRange.Usage;
                if (v.UsagePage == page && usage >= first && usage <= last && v.ReportID != exclude)
                    return v.ReportID;
            }
    }
    count = type == HidP_Input ? caps_.NumberInputButtonCaps
          : type == HidP_Output ? caps_.NumberOutputButtonCaps : caps_.NumberFeatureButtonCaps;
    if (count) {
        std::vector<HIDP_BUTTON_CAPS> buttons(count);
        if (HidP_GetButtonCaps(type, buttons.data(), &count, preparsed_) == HIDP_STATUS_SUCCESS)
            for (USHORT i = 0; i < count; i++) {
                const auto& b = buttons[i];
                USAGE first = b.IsRange ? b.Range.UsageMin : b.NotRange.Usage;
                USAGE last = b.IsRange ? b.Range.UsageMax : b.NotRange.Usage;
                if (b.UsagePage == page && usage >= first && usage <= last && b.ReportID != exclude)
                    return b.ReportID;
            }
    }
    return 0;
}

bool HidDevice::LogicalRange(HIDP_REPORT_TYPE type, USAGE page, USAGE usage, LONG& min, LONG& max) const
{
    HIDP_VALUE_CAPS caps;
    USHORT count = 1;
    if (HidP_GetSpecificValueCaps(type, page, 0, usage, &caps, &count, preparsed_) != HIDP_STATUS_SUCCESS || !count)
        return false;
    min = caps.LogicalMin;
    max = caps.LogicalMax;
    return true;
}

bool HidDevice::Send(const OutputReport& report)
{
    if (!report.Ok() || writeHandle_ == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    if (!WriteFile(writeHandle_, report.Bytes().data(), (DWORD)report.Bytes().size(), &written, nullptr)) {
        Log("write of report %02X failed (%lu)", report.Bytes()[0], GetLastError());
        return false;
    }
    return true;
}

bool HidDevice::GetFeature(UCHAR reportId, std::vector<BYTE>& data)
{
    data.assign(caps_.FeatureReportByteLength, 0);
    if (data.empty() || !reportId)
        return false;
    data[0] = reportId;
    return HidD_GetFeature(writeHandle_, data.data(), (ULONG)data.size()) != FALSE;
}

DWORD WINAPI HidDevice::ReaderThread(LPVOID self)
{
    static_cast<HidDevice*>(self)->ReadLoop();
    return 0;
}

void HidDevice::ReadLoop()
{
    std::vector<BYTE> buffer(caps_.InputReportByteLength);
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE waits[2] = { stopEvent_, ov.hEvent };
    for (;;) {
        ResetEvent(ov.hEvent);
        DWORD got = 0;
        if (!ReadFile(readHandle_, buffer.data(), (DWORD)buffer.size(), &got, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING)
                break;
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
                CancelIo(readHandle_);
                GetOverlappedResult(readHandle_, &ov, &got, TRUE);
                break;
            }
            if (!GetOverlappedResult(readHandle_, &ov, &got, FALSE))
                break;
        }
        if (got && buffer[0] == ids.state)
            ParseState(buffer.data(), got);
    }
    CloseHandle(ov.hEvent);
}

void HidDevice::ParseState(const BYTE* report, ULONG length)
{
    USAGE active[16];
    ULONG count = ARRAYSIZE(active);
    if (HidP_GetUsages(HidP_Input, usage::PagePid, 0, active, &count, preparsed_, (PCHAR)report, length)
        != HIDP_STATUS_SUCCESS)
        return;
    bool playing = false, isPaused = false, enabled = false, safety = false, power = false;
    for (ULONG i = 0; i < count; i++) {
        switch (active[i]) {
        case usage::EffectPlaying: playing = true; break;
        case usage::DevicePaused: isPaused = true; break;
        case usage::ActuatorsEnabled: enabled = true; break;
        case usage::SafetySwitch: safety = true; break;
        case usage::ActuatorPower: power = true; break;
        }
    }
    paused = isPaused;
    actuatorsEnabled = enabled;
    safetySwitch = safety;
    actuatorPower = power;
    stateReceived = true;

    ULONG index = 0;
    if (HidP_GetUsageValue(HidP_Input, usage::PagePid, 0, usage::EffectBlockIndex, &index, preparsed_, (PCHAR)report,
                           length) == HIDP_STATUS_SUCCESS && index < MaxSlots) {
        slots[index].playing = playing;
        slots[index].updated = GetTickCount();
    }
}
