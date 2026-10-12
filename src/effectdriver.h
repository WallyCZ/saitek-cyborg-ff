#pragma once
#include "ffdevice.h"

// {5A08B5F0-0527-4A07-99D5-310FE61B21FA}
DEFINE_GUID(CLSID_SaitekFF, 0x5a08b5f0, 0x0527, 0x4a07, 0x99, 0xd5, 0x31, 0x0f, 0xe6, 0x1b, 0x21, 0xfa);

extern LONG g_objects;  // live COM objects and server locks, for DllCanUnloadNow

// DirectInput force feedback effect driver. DirectInput loads it through the OEMForceFeedback CLSID
// registered for the stick and calls it with an external device id (one per attached device).
class EffectDriver : public IDirectInputEffectDriver {
public:
    EffectDriver();
    virtual ~EffectDriver();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDirectInputEffectDriver
    STDMETHODIMP DeviceID(DWORD directInputVersion, DWORD externalId, DWORD begin, DWORD internalId, LPVOID reserved) override;
    STDMETHODIMP GetVersions(LPDIDRIVERVERSIONS versions) override;
    STDMETHODIMP Escape(DWORD id, DWORD effect, LPDIEFFESCAPE escape) override;
    STDMETHODIMP SetGain(DWORD id, DWORD gain) override;
    STDMETHODIMP SendForceFeedbackCommand(DWORD id, DWORD command) override;
    STDMETHODIMP GetForceFeedbackState(DWORD id, LPDIDEVICESTATE state) override;
    STDMETHODIMP DownloadEffect(DWORD id, DWORD effectId, LPDWORD effect, LPCDIEFFECT params, DWORD flags) override;
    STDMETHODIMP DestroyEffect(DWORD id, DWORD effect) override;
    STDMETHODIMP StartEffect(DWORD id, DWORD effect, DWORD mode, DWORD count) override;
    STDMETHODIMP StopEffect(DWORD id, DWORD effect) override;
    STDMETHODIMP GetEffectStatus(DWORD id, DWORD effect, LPDWORD status) override;

private:
    static constexpr DWORD MaxDevices = 16;
    FFDevice* Device(DWORD id) { return id < MaxDevices ? devices_[id].get() : nullptr; }

    LONG refs_ = 1;
    CRITICAL_SECTION lock_;
    std::unique_ptr<FFDevice> devices_[MaxDevices];
};
