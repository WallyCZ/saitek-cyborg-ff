#include "effectdriver.h"
#include "log.h"

EffectDriver::EffectDriver()
{
    InitializeCriticalSection(&lock_);
    InterlockedIncrement(&g_objects);
}

EffectDriver::~EffectDriver()
{
    for (auto& d : devices_)
        d.reset();
    DeleteCriticalSection(&lock_);
    InterlockedDecrement(&g_objects);
}

STDMETHODIMP EffectDriver::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDirectInputEffectDriver) {
        *ppv = static_cast<IDirectInputEffectDriver*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EffectDriver::AddRef() { return InterlockedIncrement(&refs_); }

STDMETHODIMP_(ULONG) EffectDriver::Release()
{
    ULONG refs = InterlockedDecrement(&refs_);
    if (!refs)
        delete this;
    return refs;
}

STDMETHODIMP EffectDriver::DeviceID(DWORD directInputVersion, DWORD externalId, DWORD begin, DWORD internalId,
                                    LPVOID reserved)
{
    if (externalId >= MaxDevices)
        return DIERR_INVALIDPARAM;
    EnterCriticalSection(&lock_);
    HRESULT hr = DI_OK;
    auto init = static_cast<LPDIHIDFFINITINFO>(reserved);
    Log("DeviceID(version %lX, external %lu, begin %lu, internal %lu, %ls)", directInputVersion, externalId, begin,
        internalId, init && init->pwszDeviceInterface ? init->pwszDeviceInterface : L"-");
    if (begin) {
        if (!init || init->dwSize < sizeof(DIHIDFFINITINFO) || !init->pwszDeviceInterface) {
            hr = DIERR_INVALIDPARAM;
        } else {
            auto device = std::make_unique<FFDevice>();
            hr = device->Open(init->pwszDeviceInterface);
            devices_[externalId] = SUCCEEDED(hr) ? std::move(device) : nullptr;
        }
    } else {
        devices_[externalId].reset();
    }
    LeaveCriticalSection(&lock_);
    return hr;
}

STDMETHODIMP EffectDriver::GetVersions(LPDIDRIVERVERSIONS versions)
{
    if (!versions || versions->dwSize < sizeof(DIDRIVERVERSIONS))
        return DIERR_INVALIDPARAM;
    versions->dwFirmwareRevision = versions->dwHardwareRevision = 0;
    versions->dwFFDriverVersion = 0x00020000;
    for (auto& d : devices_)
        if (d) {
            d->GetVersions(versions);
            break;
        }
    return DI_OK;
}

STDMETHODIMP EffectDriver::Escape(DWORD id, DWORD effect, LPDIEFFESCAPE escape)
{
    Log("Escape(%lu, effect %lu, command %08lX) not supported", id, effect, escape ? escape->dwCommand : 0);
    return DIERR_UNSUPPORTED;
}

STDMETHODIMP EffectDriver::SetGain(DWORD id, DWORD gain)
{
    FFDevice* d = Device(id);
    return d ? d->SetGain(gain) : DIERR_INPUTLOST;
}

STDMETHODIMP EffectDriver::SendForceFeedbackCommand(DWORD id, DWORD command)
{
    FFDevice* d = Device(id);
    return d ? d->SendCommand(command) : DIERR_INPUTLOST;
}

STDMETHODIMP EffectDriver::GetForceFeedbackState(DWORD id, LPDIDEVICESTATE state)
{
    FFDevice* d = Device(id);
    if (!state)
        return E_POINTER;
    return d ? d->GetState(state) : DIERR_INPUTLOST;
}

STDMETHODIMP EffectDriver::DownloadEffect(DWORD id, DWORD effectId, LPDWORD effect, LPCDIEFFECT params, DWORD flags)
{
    FFDevice* d = Device(id);
    if (!effect || !params)
        return E_POINTER;
    return d ? d->Download(effectId, effect, params, flags) : DIERR_INPUTLOST;
}

STDMETHODIMP EffectDriver::DestroyEffect(DWORD id, DWORD effect)
{
    FFDevice* d = Device(id);
    return d ? d->Destroy(effect) : DIERR_INPUTLOST;
}

STDMETHODIMP EffectDriver::StartEffect(DWORD id, DWORD effect, DWORD mode, DWORD count)
{
    FFDevice* d = Device(id);
    return d ? d->Start(effect, mode, count) : DIERR_INPUTLOST;
}

STDMETHODIMP EffectDriver::StopEffect(DWORD id, DWORD effect)
{
    FFDevice* d = Device(id);
    return d ? d->Stop(effect) : DIERR_INPUTLOST;
}

STDMETHODIMP EffectDriver::GetEffectStatus(DWORD id, DWORD effect, LPDWORD status)
{
    FFDevice* d = Device(id);
    if (!status)
        return E_POINTER;
    return d ? d->GetStatus(effect, status) : DIERR_INPUTLOST;
}
