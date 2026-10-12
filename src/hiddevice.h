#pragma once
#include <windows.h>
#include <hidsdi.h>
#include <atomic>
#include <vector>

// HID usages used by the stick (HID Physical Interface Device page and friends)
namespace usage {
constexpr USAGE PageGenericDesktop = 0x01, PageOrdinal = 0x0A, PagePid = 0x0F, PageVendor = 0xFF;
constexpr USAGE X = 0x30, Y = 0x31;

constexpr USAGE EffectBlockIndex = 0x22, ParamBlockOffset = 0x23;
constexpr USAGE ConstantForce = 0x26, Ramp = 0x27, Square = 0x30, Sine = 0x31, Triangle = 0x32,
                SawtoothUp = 0x33, SawtoothDown = 0x34, Spring = 0x40, Damper = 0x41, Inertia = 0x42,
                Friction = 0x43;
constexpr USAGE Duration = 0x50, TriggerButton = 0x53, TriggerRepeat = 0x54, DirectionEnable = 0x56,
                Direction = 0x57, StartDelay = 0xA7;
constexpr USAGE AttackLevel = 0x5B, AttackTime = 0x5C, FadeLevel = 0x5D, FadeTime = 0x5E;
constexpr USAGE CpOffset = 0x60, PositiveCoefficient = 0x61, NegativeCoefficient = 0x62,
                PositiveSaturation = 0x63, NegativeSaturation = 0x64, DeadBand = 0x65;
constexpr USAGE Magnitude = 0x70, Offset = 0x6F, Phase = 0x71, Period = 0x72;
constexpr USAGE OpStart = 0x79, OpStartSolo = 0x7A, OpStop = 0x7B, LoopCount = 0x7C;
constexpr USAGE DeviceGain = 0x7E;
constexpr USAGE RamPoolSize = 0x80, SimultaneousEffectsMax = 0x83, PoolAlignment = 0x84;
constexpr USAGE EnvelopeBlockSize = 0x5A, ConditionBlockSize = 0x5F, PeriodicBlockSize = 0x6E,
                ConstantBlockSize = 0x73;
constexpr USAGE EffectPlaying = 0x94;
constexpr USAGE EnableActuators = 0x97, DisableActuators = 0x98, StopAllEffects = 0x99, DeviceReset = 0x9A,
                DevicePause = 0x9B, DeviceContinue = 0x9C;
constexpr USAGE DevicePaused = 0x9F, ActuatorsEnabled = 0xA0, SafetySwitch = 0xA4, ActuatorPower = 0xA6;

// Saitek/Immersion vendor control report: an array selecting a sub-command plus a 16-bit value.
// The sub-command numbers match the I-FORCE protocol. Sub-command 3 sets the stick's own centering spring:
// a signed 8-bit strength, 0 (off) to 127 (strongest); negative values also turn it off.
constexpr USAGE VendorCommandBase = 0x310, VendorValue = 0x303;
constexpr USAGE VendorCentering = VendorCommandBase + 3;
}

class HidDevice;

// An output report assembled with the HidP_* API, so field positions come from the device's own descriptor
class OutputReport {
public:
    OutputReport(const HidDevice& device, UCHAR reportId);
    OutputReport& Value(USAGE page, USAGE usage, ULONG raw);
    OutputReport& Scaled(USAGE page, USAGE usage, LONG physical);
    OutputReport& Usage(USAGE page, USAGE usage);
    bool Ok() const { return ok_; }
    const std::vector<BYTE>& Bytes() const { return data_; }

private:
    const HidDevice& device_;
    std::vector<BYTE> data_;
    bool ok_ = true;
};

// Status of one effect slot as reported by the stick's PID state input report
struct SlotStatus {
    std::atomic<bool> playing{ false };
    std::atomic<DWORD> updated{ 0 };  // GetTickCount of the last report for this slot
};

class HidDevice {
public:
    ~HidDevice() { Close(); }
    bool Open(const wchar_t* path);
    void Close();
    bool IsOpen() const { return writeHandle_ != INVALID_HANDLE_VALUE; }

    bool Send(const OutputReport& report);
    bool SendVendor(USAGE command, USHORT value);
    bool SetCentering(int strength) { return SendVendor(usage::VendorCentering, (USHORT)strength); }  // 0..127
    bool GetFeature(UCHAR reportId, std::vector<BYTE>& data);

    // Report IDs, found by a usage that only that report contains
    struct ReportIds {
        UCHAR setEffect, envelope, condition, periodic, constant, effectOperation, deviceControl, deviceGain,
              vendor, pool, state;
    } ids = {};

    PHIDP_PREPARSED_DATA Preparsed() const { return preparsed_; }
    const HIDP_CAPS& Caps() const { return caps_; }
    USHORT VersionNumber() const { return attributes_.VersionNumber; }
    bool LogicalRange(HIDP_REPORT_TYPE type, USAGE page, USAGE usage, LONG& min, LONG& max) const;

    // State input report
    static constexpr int MaxSlots = 32;
    SlotStatus slots[MaxSlots];
    std::atomic<bool> paused{ false }, actuatorsEnabled{ false }, safetySwitch{ false }, actuatorPower{ true };
    std::atomic<bool> stateReceived{ false };

private:
    UCHAR FindReportId(HIDP_REPORT_TYPE type, USAGE page, USAGE usage, UCHAR exclude = 0) const;
    static DWORD WINAPI ReaderThread(LPVOID self);
    void ReadLoop();
    void ParseState(const BYTE* report, ULONG length);

    HANDLE writeHandle_ = INVALID_HANDLE_VALUE;
    HANDLE readHandle_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
    HANDLE thread_ = nullptr;
    PHIDP_PREPARSED_DATA preparsed_ = nullptr;
    HIDP_CAPS caps_ = {};
    HIDD_ATTRIBUTES attributes_ = { sizeof(HIDD_ATTRIBUTES) };
};
