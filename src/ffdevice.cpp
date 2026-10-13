#include "ffdevice.h"
#include "log.h"
#include <algorithm>
#include <math.h>

namespace {

constexpr LONG Nominal = DI_FFNOMINALMAX;  // 10000, DirectInput's full scale

class Lock {
public:
    explicit Lock(CRITICAL_SECTION& cs) : cs_(cs) { EnterCriticalSection(&cs_); }
    ~Lock() { LeaveCriticalSection(&cs_); }

private:
    CRITICAL_SECTION& cs_;
};

LONG Clamp(LONG v, LONG lo, LONG hi) { return v < lo ? lo : v > hi ? hi : v; }

// DirectInput times are microseconds, the stick works in milliseconds
DWORD Ms(DWORD us, DWORD max) { return us == INFINITE ? 0xFFFF : std::min<DWORD>(us / 1000, max); }

int AxisInstance(DWORD objectId) { return DIDFT_GETINSTANCE(objectId); }

int Align(int size, int alignment) { return (size + alignment - 1) / alignment * alignment; }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// ParamMemory

void ParamMemory::Reset(int size, int alignment)
{
    used_.clear();
    size_ = size;
    alignment_ = alignment > 0 ? alignment : 1;
}

int ParamMemory::Allocate(int size)
{
    size = Align(size, alignment_);
    int offset = 0;
    for (size_t i = 0; i <= used_.size(); i++) {
        int end = i < used_.size() ? used_[i].offset : size_;
        if (end - offset >= size) {
            used_.insert(used_.begin() + i, Block{ offset, size });
            return offset;
        }
        if (i < used_.size())
            offset = used_[i].offset + used_[i].size;
    }
    return -1;
}

void ParamMemory::Free(int offset)
{
    for (auto it = used_.begin(); it != used_.end(); ++it)
        if (it->offset == offset) {
            used_.erase(it);
            return;
        }
}

int ParamMemory::Used() const
{
    int total = 0;
    for (const auto& b : used_)
        total += b.size;
    return total;
}

// ---------------------------------------------------------------------------------------------------------------
// FFDevice

FFDevice::FFDevice() { InitializeCriticalSection(&lock_); }

FFDevice::~FFDevice()
{
    Close();
    DeleteCriticalSection(&lock_);
}

HRESULT FFDevice::Open(const wchar_t* path)
{
    Lock lock(lock_);
    if (!hid_.Open(path))
        return DIERR_DEVICENOTREG;

    // Device memory layout from the PID pool feature report, with the values the original driver used as fallback
    int ramSize = 300, maxEffects = 10, alignment = 2;
    std::vector<BYTE> pool;
    if (hid_.GetFeature(hid_.ids.pool, pool)) {
        auto value = [&](USAGE u, int& out) {
            ULONG v;
            if (HidP_GetUsageValue(HidP_Feature, usage::PagePid, 0, u, &v, hid_.Preparsed(), (PCHAR)pool.data(),
                                   (ULONG)pool.size()) == HIDP_STATUS_SUCCESS && v)
                out = std::max(out, (int)v);
        };
        int ram = 0, effects = 0, align = 0;
        value(usage::RamPoolSize, ram);
        value(usage::SimultaneousEffectsMax, effects);
        value(usage::PoolAlignment, align);
        value(usage::ConstantBlockSize, constantSize_);
        value(usage::PeriodicBlockSize, periodicSize_);
        value(usage::ConditionBlockSize, conditionSize_);
        value(usage::EnvelopeBlockSize, envelopeSize_);
        if (ram)
            ramSize = ram;
        if (effects)
            maxEffects = effects;
        if (align)
            alignment = align;
    }
    LONG min, max;
    if (hid_.LogicalRange(HidP_Output, usage::PageOrdinal, 1, min, max))
        ramSize = std::min(ramSize, (int)max + 1);
    if (hid_.LogicalRange(HidP_Output, usage::PagePid, usage::EffectBlockIndex, min, max))
        maxEffects = std::min(maxEffects, (int)max + 1);
    maxEffects = std::min(maxEffects, HidDevice::MaxSlots);

    slots_.assign(maxEffects, nullptr);
    memory_.Reset(ramSize, alignment);
    settings_ = Settings::Load();
    session_ = CreateEventW(nullptr, TRUE, FALSE, SessionEventName);
    stopWatcher_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    watcher_ = CreateThread(nullptr, 0, SettingsWatcher, this, 0, nullptr);
    Log("device memory %d bytes (alignment %d), %d effect slots, blocks: constant %d periodic %d condition %d "
        "envelope %d",
        ramSize, alignment, maxEffects, constantSize_, periodicSize_, conditionSize_, envelopeSize_);
    return DI_OK;
}

void FFDevice::Close()
{
    StopWatchingSettings();  // outside the lock: the watcher takes it
    Lock lock(lock_);
    if (!hid_.IsOpen())
        return;
    Control(usage::StopAllEffects);
    DestroyAll();
    // Back to the centering spring the user chose for when no game drives the stick
    hid_.SetCentering(Settings::Load().CenteringValue());
    hid_.Close();
    if (session_) {
        CloseHandle(session_);
        session_ = nullptr;
    }
    Log("device closed");
}

DWORD WINAPI FFDevice::SettingsWatcher(LPVOID self)
{
    static_cast<FFDevice*>(self)->WatchSettings();
    return 0;
}

// Applies changes from the Force Feedback page right away: reloads the settings and sends the effects on the
// stick again with the new strengths (only reports whose content changed go out)
void FFDevice::WatchSettings()
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SaitekFF", 0, nullptr, 0, KEY_NOTIFY | KEY_READ, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return;
    HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE waits[2] = { stopWatcher_, changed };
    while (RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE) == ERROR_SUCCESS &&
           WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        Sleep(100);  // the page writes several values in a row
        Lock lock(lock_);
        settings_ = Settings::Load();
        int resent = 0;
        for (Effect* e : slots_)
            if (e && SUCCEEDED(SendEffect(*e)))
                resent++;
        Log("settings changed: gain %lu, springs %lu, dampers %lu; %d effects updated", settings_.gain,
            settings_.springGain, settings_.damperGain, resent);
    }
    CloseHandle(changed);
    RegCloseKey(key);
}

void FFDevice::StopWatchingSettings()
{
    if (watcher_) {
        SetEvent(stopWatcher_);
        WaitForSingleObject(watcher_, 2000);
        CloseHandle(watcher_);
        watcher_ = nullptr;
    }
    if (stopWatcher_) {
        CloseHandle(stopWatcher_);
        stopWatcher_ = nullptr;
    }
}

void FFDevice::GetVersions(LPDIDRIVERVERSIONS versions)
{
    Lock lock(lock_);
    versions->dwFirmwareRevision = hid_.VersionNumber();
    versions->dwHardwareRevision = hid_.VersionNumber();
    versions->dwFFDriverVersion = 0x00020000;
}

HRESULT FFDevice::SetGain(DWORD gain)
{
    Lock lock(lock_);
    if (!hid_.ids.deviceGain)
        return DI_OK;
    OutputReport report(hid_, hid_.ids.deviceGain);
    report.Scaled(usage::PagePid, usage::DeviceGain, Clamp((LONG)gain, 0, Nominal));
    return hid_.Send(report) ? DI_OK : DIERR_GENERIC;
}

HRESULT FFDevice::SendCommand(DWORD command)
{
    Lock lock(lock_);
    Log("command %08lX", command);
    switch (command) {
    case DISFFC_RESET:
        DestroyAll();
        if (!Control(usage::DeviceReset))
            return DIERR_GENERIC;
        // DirectInput applications drive centering with their own spring effects
        hid_.SetCentering(0);
        settings_ = Settings::Load();
        return DI_OK;
    case DISFFC_STOPALL:
        for (auto& [handle, e] : effects_)
            e->started = false;
        return Control(usage::StopAllEffects) ? DI_OK : DIERR_GENERIC;
    case DISFFC_PAUSE:
        return Control(usage::DevicePause) ? DI_OK : DIERR_GENERIC;
    case DISFFC_CONTINUE:
        return Control(usage::DeviceContinue) ? DI_OK : DIERR_GENERIC;
    case DISFFC_SETACTUATORSON:
        return Control(usage::EnableActuators) ? DI_OK : DIERR_GENERIC;
    case DISFFC_SETACTUATORSOFF:
        return Control(usage::DisableActuators) ? DI_OK : DIERR_GENERIC;
    }
    return DIERR_INVALIDPARAM;
}

HRESULT FFDevice::GetState(LPDIDEVICESTATE state)
{
    Lock lock(lock_);
    if (state->dwSize < sizeof(DIDEVICESTATE))
        return DIERR_INVALIDPARAM;
    bool any = false, playing = false;
    for (auto& [handle, e] : effects_) {
        any = any || e->slot >= 0;
        playing = playing || IsPlaying(*e);
    }
    DWORD s = DIGFFS_USERFFSWITCHON;
    if (!any)
        s |= DIGFFS_EMPTY;
    if (hid_.paused)
        s |= DIGFFS_PAUSED;
    else if (!playing)
        s |= DIGFFS_STOPPED;
    s |= hid_.actuatorsEnabled ? DIGFFS_ACTUATORSON : DIGFFS_ACTUATORSOFF;
    s |= hid_.actuatorPower || !hid_.stateReceived ? DIGFFS_POWERON : DIGFFS_POWEROFF;
    s |= hid_.safetySwitch ? DIGFFS_SAFETYSWITCHON : DIGFFS_SAFETYSWITCHOFF;
    state->dwState = s;
    state->dwLoad = memory_.Size() ? memory_.Used() * 100 / memory_.Size() : 0;
    return DI_OK;
}

bool FFDevice::IsSupported(DWORD type) const
{
    switch (type) {
    case usage::ConstantForce:
        return hid_.ids.constant != 0;
    case usage::Ramp:
    case usage::Square:
    case usage::Sine:
    case usage::Triangle:
    case usage::SawtoothUp:
    case usage::SawtoothDown:
        return hid_.ids.periodic != 0;
    case usage::Spring:
    case usage::Damper:
    case usage::Inertia:
    case usage::Friction:
        return hid_.ids.condition != 0;
    }
    return false;
}

int FFDevice::BlockSize(DWORD type, int block) const
{
    if (IsCondition(type))
        return conditionSize_;
    if (block == 1)
        return envelopeSize_;
    return type == usage::ConstantForce ? constantSize_ : periodicSize_;
}

FFDevice::Effect* FFDevice::Find(DWORD handle)
{
    auto it = effects_.find(handle);
    return it == effects_.end() ? nullptr : it->second.get();
}

HRESULT FFDevice::Merge(Effect& e, LPCDIEFFECT p, DWORD flags)
{
    if (flags & DIEP_DURATION)
        e.duration = p->dwDuration;
    if (flags & DIEP_GAIN)
        e.gain = std::min<DWORD>(p->dwGain, Nominal);
    if (flags & DIEP_TRIGGERBUTTON)
        e.triggerButton = p->dwTriggerButton;
    if (flags & DIEP_TRIGGERREPEATINTERVAL)
        e.triggerRepeat = p->dwTriggerRepeatInterval;
    if (flags & DIEP_AXES) {
        if (!p->cAxes || p->cAxes > 2 || !p->rgdwAxes)
            return DIERR_INVALIDPARAM;
        e.axisCount = p->cAxes;
        for (DWORD i = 0; i < p->cAxes; i++)
            e.axes[i] = p->rgdwAxes[i];
    }
    if (flags & DIEP_DIRECTION) {
        if (!p->rglDirection || !p->cAxes)
            return DIERR_INVALIDPARAM;
        e.coords = p->dwFlags & (DIEFF_CARTESIAN | DIEFF_POLAR | DIEFF_SPHERICAL);
        for (DWORD i = 0; i < std::min<DWORD>(p->cAxes, 2); i++)
            e.direction[i] = p->rglDirection[i];
    }
    if (flags & DIEP_ENVELOPE) {
        e.hasEnvelope = p->lpEnvelope != nullptr;
        if (e.hasEnvelope)
            e.envelope = *p->lpEnvelope;
    }
    if (flags & DIEP_TYPESPECIFICPARAMS) {
        if (!p->lpvTypeSpecificParams)
            return DIERR_INVALIDPARAM;
        const BYTE* params = static_cast<const BYTE*>(p->lpvTypeSpecificParams);
        e.params.assign(params, params + p->cbTypeSpecificParams);
    }
    if ((flags & DIEP_STARTDELAY) && p->dwSize >= sizeof(DIEFFECT))
        e.startDelay = p->dwStartDelay;
    return DI_OK;
}

HRESULT FFDevice::Validate(const Effect& e) const
{
    if (!e.axisCount)
        return DIERR_INVALIDPARAM;
    size_t size = e.params.size();
    switch (e.type) {
    case usage::ConstantForce:
        return size == sizeof(DICONSTANTFORCE) ? DI_OK : DIERR_INVALIDPARAM;
    case usage::Ramp:
        return size == sizeof(DIRAMPFORCE) ? DI_OK : DIERR_INVALIDPARAM;
    case usage::Square:
    case usage::Sine:
    case usage::Triangle:
    case usage::SawtoothUp:
    case usage::SawtoothDown:
        return size == sizeof(DIPERIODIC) ? DI_OK : DIERR_INVALIDPARAM;
    default:
        return size == sizeof(DICONDITION) || size == 2 * sizeof(DICONDITION) ? DI_OK : DIERR_INVALIDPARAM;
    }
}

HRESULT FFDevice::Download(DWORD effectType, LPDWORD handle, LPCDIEFFECT effect, DWORD flags)
{
    Lock lock(lock_);
    if (!hid_.IsOpen())
        return DIERR_NOTACQUIRED;
    if (!IsSupported(effectType))
        return DIERR_UNSUPPORTED;

    Effect* existing = *handle ? Find(*handle) : nullptr;
    if (*handle && !existing)
        return DIERR_INVALIDPARAM;

    // Work on a copy so a rejected update leaves the effect untouched
    Effect updated = existing ? *existing : Effect{};
    updated.type = effectType;
    HRESULT hr = Merge(updated, effect, flags);
    if (SUCCEEDED(hr))
        hr = Validate(updated);
    if (FAILED(hr) || (flags & DIEP_NODOWNLOAD))
        return hr;

    Effect* e;
    if (existing) {
        *existing = std::move(updated);
        e = existing;
    } else {
        updated.handle = nextHandle_++;
        auto owned = std::make_unique<Effect>(std::move(updated));
        e = owned.get();
        effects_[e->handle] = std::move(owned);
    }
    if (!MakeResident(*e)) {
        Log("download of effect %02X failed: device full", effectType);
        if (!existing)
            effects_.erase(e->handle);
        return DIERR_DEVICEFULL;
    }
    hr = SendEffect(*e);
    *handle = e->handle;
    Log("download effect %lu type %02X flags %08lX slot %d -> %08lX", e->handle, e->type, flags, e->slot, hr);
    if (SUCCEEDED(hr) && (flags & DIEP_START))
        hr = Start(e->handle, 0, 1);
    return hr;
}

HRESULT FFDevice::Destroy(DWORD handle)
{
    Lock lock(lock_);
    Effect* e = Find(handle);
    if (!e)
        return DIERR_INVALIDPARAM;
    Unload(*e);
    effects_.erase(handle);
    Log("destroy effect %lu", handle);
    return DI_OK;
}

HRESULT FFDevice::Start(DWORD handle, DWORD mode, DWORD count)
{
    Lock lock(lock_);
    Effect* e = Find(handle);
    if (!e)
        return DIERR_INVALIDPARAM;
    if (e->slot < 0) {
        // The effect was evicted from the stick to make room for others: bring it back
        if (mode & DIES_NODOWNLOAD)
            return DIERR_NOTDOWNLOADED;
        if (!MakeResident(*e))
            return DIERR_DEVICEFULL;
        HRESULT hr = SendEffect(*e);
        if (FAILED(hr))
            return hr;
    }
    if (mode & DIES_SOLO)
        for (auto& [h, other] : effects_)
            other->started = false;
    // Loop count 255 makes the stick repeat the effect until it is stopped
    DWORD loops = count == INFINITE || count > 254 ? 255 : count;
    if (!Operation(e->slot, (mode & DIES_SOLO) ? usage::OpStartSolo : usage::OpStart, loops))
        return DIERR_GENERIC;
    e->started = true;
    e->startTick = GetTickCount();
    e->loops = count;
    e->lastUse = ++useCounter_;
    return DI_OK;
}

HRESULT FFDevice::Stop(DWORD handle)
{
    Lock lock(lock_);
    Effect* e = Find(handle);
    if (!e)
        return DIERR_INVALIDPARAM;
    e->started = false;
    if (e->slot < 0)
        return DI_OK;
    return Operation(e->slot, usage::OpStop, 0) ? DI_OK : DIERR_GENERIC;
}

HRESULT FFDevice::GetStatus(DWORD handle, LPDWORD status)
{
    Lock lock(lock_);
    Effect* e = Find(handle);
    if (!e)
        return DIERR_INVALIDPARAM;
    *status = IsPlaying(*e) ? DIEGES_PLAYING : 0;
    return DI_OK;
}

bool FFDevice::IsPlaying(Effect& e)
{
    if (!e.started || e.slot < 0)
        return false;
    // The stick reports each slot's playing flag a few times a second; once a report newer than the start
    // has arrived, trust it
    const SlotStatus& s = hid_.slots[e.slot];
    if ((LONG)(s.updated - e.startTick) > 150) {
        if (!s.playing)
            e.started = false;
        return s.playing;
    }
    if (e.duration == INFINITE || e.loops == INFINITE)
        return true;
    ULONGLONG total = e.startDelay / 1000 + (ULONGLONG)(e.duration / 1000) * e.loops;
    return GetTickCount() - e.startTick <= total + 50;
}

bool FFDevice::MakeResident(Effect& e)
{
    e.lastUse = ++useCounter_;
    if (e.slot >= 0)
        return true;
    for (;;) {
        auto free = std::find(slots_.begin(), slots_.end(), nullptr);
        if (free != slots_.end()) {
            int b0 = memory_.Allocate(BlockSize(e.type, 0));
            int b1 = b0 >= 0 ? memory_.Allocate(BlockSize(e.type, 1)) : -1;
            if (b1 >= 0) {
                e.slot = (int)(free - slots_.begin());
                e.block[0] = b0;
                e.block[1] = b1;
                *free = &e;
                for (auto& s : e.sent)
                    s.clear();
                return true;
            }
            if (b0 >= 0)
                memory_.Free(b0);
        }
        if (!Evict(&e))
            return false;
    }
}

bool FFDevice::Evict(const Effect* keep)
{
    Effect* victim = nullptr;
    for (Effect* e : slots_)
        if (e && e != keep && !IsPlaying(*e) && (!victim || e->lastUse < victim->lastUse))
            victim = e;
    if (!victim)
        return false;
    Log("evicting effect %lu from slot %d", victim->handle, victim->slot);
    Unload(*victim);
    return true;
}

void FFDevice::Unload(Effect& e)
{
    if (e.slot < 0)
        return;
    if (IsPlaying(e))
        Operation(e.slot, usage::OpStop, 0);
    // An effect armed on a fire button would keep reacting to the button: disarm it
    if (e.triggerButton != DIEB_NOTRIGGER && !e.sent[SentEffect].empty()) {
        DWORD trigger = e.triggerButton;
        e.triggerButton = DIEB_NOTRIGGER;
        SendEffect(e);
        e.triggerButton = trigger;
    }
    memory_.Free(e.block[0]);
    memory_.Free(e.block[1]);
    slots_[e.slot] = nullptr;
    e.slot = e.block[0] = e.block[1] = -1;
    e.started = false;
    for (auto& s : e.sent)
        s.clear();
}

void FFDevice::DestroyAll()
{
    effects_.clear();
    std::fill(slots_.begin(), slots_.end(), nullptr);
    memory_.Clear();
}

bool FFDevice::SendIfChanged(Effect& e, Sent which, const OutputReport& report)
{
    if (!report.Ok())
        return false;
    if (report.Bytes() == e.sent[which])
        return true;
    if (!hid_.Send(report))
        return false;
    e.sent[which] = report.Bytes();
    return true;
}

HRESULT FFDevice::SendEffect(Effect& e)
{
    // Effect gain from the game times the strength settings from the Game Controllers panel
    DWORD typeGain = e.type == usage::Spring ? settings_.springGain
                   : IsCondition(e.type) ? settings_.damperGain : 100;
    int gain = MulDiv((int)e.gain, (int)(settings_.gain * typeGain), 100 * 100);
    auto scale = [&](LONG v) { return (LONG)MulDiv(v, gain, Nominal); };
    const bool twoAxes = e.axisCount == 2;

    // Direction in hundredths of a degree, 0 = north, clockwise (DirectInput polar convention)
    LONG direction = 0;
    if (twoAxes) {
        if (e.coords & DIEFF_POLAR)
            direction = e.direction[0];
        else if (e.coords & DIEFF_SPHERICAL)
            direction = e.direction[0] + 9000;
        else
            direction = (LONG)lround(atan2((double)e.direction[0], -(double)e.direction[1]) * 18000.0 / 3.14159265358979);
        direction = (direction % 36000 + 36000) % 36000;
    }
    // A single axis effect with a negative cartesian direction pushes the other way
    const bool invert = e.axisCount == 1 && (e.coords & DIEFF_CARTESIAN) && e.direction[0] < 0;
    bool directional = twoAxes;
    USAGE deviceType = (USAGE)e.type;
    ULONG envelopeOffset = 0xFFFF;  // "no envelope"
    bool ok = true;

    if (IsCondition(e.type)) {
        // The stick has no inertia and friction effects; like the original driver, play them as damper
        if (e.type == usage::Inertia || e.type == usage::Friction)
            deviceType = usage::Damper;
        const DICONDITION* given = reinterpret_cast<const DICONDITION*>(e.params.data());
        size_t count = e.params.size() / sizeof(DICONDITION);
        DICONDITION cond[2] = {};
        if (twoAxes && count == 1) {
            cond[0] = given[0];  // one condition along the effect direction
        } else {
            directional = false;
            for (DWORD i = 0; i < e.axisCount; i++) {
                int axis = AxisInstance(e.axes[i]);
                if (axis < 2)
                    cond[axis] = given[std::min<size_t>(i, count - 1)];
            }
        }
        // Parameter block 0 holds the X axis (or directional) condition, block 1 the Y axis condition
        for (int i = 0; i < 2; i++) {
            const DICONDITION& c = cond[i];
            OutputReport r(hid_, hid_.ids.condition);
            r.Value(usage::PagePid, usage::ParamBlockOffset, e.block[i])
                .Scaled(usage::PagePid, usage::CpOffset, Clamp(c.lOffset, -Nominal, Nominal))
                .Scaled(usage::PagePid, usage::PositiveCoefficient, Clamp(scale(c.lPositiveCoefficient), -Nominal, Nominal))
                .Scaled(usage::PagePid, usage::NegativeCoefficient, Clamp(scale(c.lNegativeCoefficient), -Nominal, Nominal))
                .Scaled(usage::PagePid, usage::DeadBand, Clamp(c.lDeadBand, 0, Nominal))
                .Scaled(usage::PagePid, usage::PositiveSaturation,
                        c.dwPositiveSaturation ? Clamp((LONG)c.dwPositiveSaturation, 0, Nominal) : Nominal)
                .Scaled(usage::PagePid, usage::NegativeSaturation,
                        c.dwNegativeSaturation ? Clamp((LONG)c.dwNegativeSaturation, 0, Nominal) : Nominal);
            ok &= SendIfChanged(e, i == 0 ? SentParam0 : SentParam1, r);
        }
        envelopeOffset = e.block[1];
    } else {
        if (e.type == usage::ConstantForce) {
            LONG magnitude = Clamp(scale(reinterpret_cast<const DICONSTANTFORCE*>(e.params.data())->lMagnitude),
                                   -Nominal, Nominal);
            if (invert)
                magnitude = -magnitude;
            if (directional && magnitude < 0) {
                magnitude = -magnitude;
                direction = (direction + 18000) % 36000;
            }
            OutputReport r(hid_, hid_.ids.constant);
            r.Value(usage::PagePid, usage::ParamBlockOffset, e.block[0])
                .Scaled(usage::PagePid, usage::Magnitude, magnitude);
            ok &= SendIfChanged(e, SentParam0, r);
        } else {
            LONG magnitude, offset, phase = 0;
            DWORD period;
            if (e.type == usage::Ramp) {
                // The stick has no ramp effect: emulate it with one sawtooth period spanning the duration
                const DIRAMPFORCE* ramp = reinterpret_cast<const DIRAMPFORCE*>(e.params.data());
                LONG start = scale(ramp->lStart), end = scale(ramp->lEnd);
                if (invert) {
                    start = -start;
                    end = -end;
                }
                deviceType = start < end ? usage::SawtoothUp : usage::SawtoothDown;
                magnitude = labs(start - end) / 2;
                offset = (start + end) / 2;
                period = Ms(e.duration, 10000);
            } else {
                const DIPERIODIC* p = reinterpret_cast<const DIPERIODIC*>(e.params.data());
                magnitude = scale((LONG)std::min<DWORD>(p->dwMagnitude, Nominal));
                offset = scale(p->lOffset);
                phase = (LONG)(p->dwPhase % 36000) + (invert ? 18000 : 0);
                period = Ms(p->dwPeriod, 10000);
            }
            magnitude = Clamp(magnitude, 0, Nominal);
            offset = Clamp(offset, -(Nominal - magnitude), Nominal - magnitude);
            OutputReport r(hid_, hid_.ids.periodic);
            r.Value(usage::PagePid, usage::ParamBlockOffset, e.block[0])
                .Scaled(usage::PagePid, usage::Magnitude, magnitude)
                .Scaled(usage::PagePid, usage::Offset, offset)
                .Scaled(usage::PagePid, usage::Phase, phase % 36000 / 100)
                .Value(usage::PagePid, usage::Period, period);
            ok &= SendIfChanged(e, SentParam0, r);
        }
        if (e.hasEnvelope && (e.envelope.dwAttackTime || e.envelope.dwFadeTime) && hid_.ids.envelope) {
            OutputReport r(hid_, hid_.ids.envelope);
            r.Value(usage::PagePid, usage::ParamBlockOffset, e.block[1])
                .Value(usage::PagePid, usage::AttackTime, Ms(e.envelope.dwAttackTime, 10000))
                .Scaled(usage::PagePid, usage::AttackLevel, Clamp(scale((LONG)e.envelope.dwAttackLevel), 0, Nominal))
                .Value(usage::PagePid, usage::FadeTime, Ms(e.envelope.dwFadeTime, 10000))
                .Scaled(usage::PagePid, usage::FadeLevel, Clamp(scale((LONG)e.envelope.dwFadeLevel), 0, Nominal));
            ok &= SendIfChanged(e, SentParam1, r);
            envelopeOffset = e.block[1];
        }
    }

    OutputReport r(hid_, hid_.ids.setEffect);
    r.Value(usage::PagePid, usage::EffectBlockIndex, e.slot).Usage(usage::PagePid, deviceType);
    if (e.triggerButton != DIEB_NOTRIGGER)
        r.Value(usage::PagePid, usage::TriggerButton, std::min(AxisInstance(e.triggerButton) + 1, 12));
    if (directional) {
        r.Usage(usage::PagePid, usage::DirectionEnable).Scaled(usage::PagePid, usage::Direction, direction / 100);
    } else {
        for (DWORD i = 0; i < e.axisCount; i++) {
            int axis = AxisInstance(e.axes[i]);
            if (axis < 2)
                r.Usage(usage::PageGenericDesktop, axis == 0 ? usage::X : usage::Y);
        }
    }
    r.Value(usage::PagePid, usage::Duration, Ms(e.duration, 0xFFFE))
        .Value(usage::PagePid, usage::TriggerRepeat, Ms(e.triggerRepeat, 0xFFFE))
        .Value(usage::PageOrdinal, 1, e.block[0])
        .Value(usage::PageOrdinal, 2, envelopeOffset)
        .Value(usage::PagePid, usage::StartDelay, Ms(e.startDelay, 0xFFFE));
    ok &= SendIfChanged(e, SentEffect, r);
    return ok ? DI_OK : DIERR_GENERIC;
}

bool FFDevice::Operation(int slot, USAGE op, DWORD loops)
{
    OutputReport r(hid_, hid_.ids.effectOperation);
    r.Value(usage::PagePid, usage::EffectBlockIndex, slot).Usage(usage::PagePid, op).Value(usage::PagePid, usage::LoopCount, loops);
    return hid_.Send(r);
}

bool FFDevice::Control(USAGE command)
{
    OutputReport r(hid_, hid_.ids.deviceControl);
    r.Usage(usage::PagePid, command);
    return hid_.Send(r);
}
