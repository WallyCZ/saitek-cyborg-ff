#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <dinputd.h>
#include <map>
#include <memory>
#include <vector>
#include "hiddevice.h"
#include "settings.h"

// Allocator for the stick's parameter RAM. The stick is a "host managed" PID device: the driver decides
// where in the device memory each effect's parameter blocks live and passes the offsets in the reports.
class ParamMemory {
public:
    void Reset(int size, int alignment);
    int Allocate(int size);  // returns offset or -1
    void Free(int offset);
    void Clear() { used_.clear(); }
    int Used() const;
    int Size() const { return size_; }

private:
    struct Block { int offset, size; };
    std::vector<Block> used_;  // sorted by offset
    int size_ = 0, alignment_ = 1;
};

// One force feedback device as seen by one DirectInput effect driver instance
class FFDevice {
public:
    FFDevice();
    ~FFDevice();

    HRESULT Open(const wchar_t* path);
    void Close();
    void GetVersions(LPDIDRIVERVERSIONS versions);
    HRESULT SetGain(DWORD gain);
    HRESULT SendCommand(DWORD command);
    HRESULT GetState(LPDIDEVICESTATE state);
    HRESULT Download(DWORD effectType, LPDWORD handle, LPCDIEFFECT effect, DWORD flags);
    HRESULT Destroy(DWORD handle);
    HRESULT Start(DWORD handle, DWORD mode, DWORD count);
    HRESULT Stop(DWORD handle);
    HRESULT GetStatus(DWORD handle, LPDWORD status);

private:
    enum Sent { SentEffect, SentParam0, SentParam1, SentCount };

    struct Effect {
        DWORD handle = 0;
        DWORD type = 0;  // DirectInput effect id from the registry = HID usage of the effect type

        // DirectInput parameters, merged across partial updates
        DWORD duration = INFINITE, gain = DI_FFNOMINALMAX, triggerButton = DIEB_NOTRIGGER;
        DWORD triggerRepeat = INFINITE, startDelay = 0, coords = DIEFF_POLAR;
        DWORD axisCount = 0, axes[2] = {};
        LONG direction[2] = {};
        bool hasEnvelope = false;
        DIENVELOPE envelope = {};
        std::vector<BYTE> params;

        // Device residency: an effect slot and two parameter blocks (type specific + envelope, or X + Y condition)
        int slot = -1;
        int block[2] = { -1, -1 };
        std::vector<BYTE> sent[SentCount];

        // Playback
        bool started = false;
        DWORD startTick = 0, loops = 1, lastUse = 0;
    };

    struct Reports {
        std::vector<OutputReport> param;  // parameter blocks, sent before the effect report
        std::unique_ptr<OutputReport> effect;
    };

    static bool IsCondition(DWORD type) { return type >= usage::Spring && type <= usage::Friction; }
    bool IsSupported(DWORD type) const;
    int BlockSize(DWORD type, int block) const;
    HRESULT Merge(Effect& e, LPCDIEFFECT effect, DWORD flags);
    HRESULT Validate(const Effect& e) const;
    bool MakeResident(Effect& e);
    void Unload(Effect& e);
    bool Evict(const Effect* keep);
    HRESULT SendEffect(Effect& e);
    bool SendIfChanged(Effect& e, Sent which, const OutputReport& report);
    bool Operation(int slot, USAGE op, DWORD loops);
    bool Control(USAGE command);
    bool IsPlaying(Effect& e);
    Effect* Find(DWORD handle);
    void DestroyAll();

    static DWORD WINAPI SettingsWatcher(LPVOID self);
    void WatchSettings();
    void StopWatchingSettings();

    CRITICAL_SECTION lock_;
    HidDevice hid_;
    HANDLE watcher_ = nullptr, stopWatcher_ = nullptr;
    HANDLE session_ = nullptr;  // marks a running force feedback session, see ApplyCentering
    std::map<DWORD, std::unique_ptr<Effect>> effects_;
    std::vector<Effect*> slots_;
    ParamMemory memory_;
    int constantSize_ = 2, periodicSize_ = 12, conditionSize_ = 8, envelopeSize_ = 14;
    DWORD nextHandle_ = 1, useCounter_ = 0;
    Settings settings_;
};
