#include "stdafx.h"
#include "MMAudioPlayer.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys.h>
#include <avrt.h>
#pragma comment(lib, "avrt.lib")
// REFERENCE_TIME time units per second and per millisecond

static const GUID SDL_KSDATAFORMAT_SUBTYPE_PCM = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID SDL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000
REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC / 100;


MMAudioPlayer::MMAudioPlayer()
{
}


MMAudioPlayer::~MMAudioPlayer()
{
    if (_thPlay.joinable())
    {
        _flags = _flags | FpFlagStop;
        _thPlay.join();
    }
}

std::vector<MMDeviceDesc> MMAudioPlayer::GetDeviceDescs()
{
    std::vector<MMDeviceDesc> descs;

    HRESULT hr = S_OK;
    if(!_enumerator)
    {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&_enumerator));
        if (FAILED(hr) || !_enumerator)
            return descs;
    }

    com_ptr<IMMDeviceCollection> collection;
    hr = _enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr) || !collection)
        return descs;

    uint32_t total = 0;
    hr = collection->GetCount(&total);
    if (FAILED(hr) || !collection)
        return descs;

    MMDeviceDesc desc = {};
    for (int id = 0; id < total; ++id)
    {
        com_ptr<IMMDevice> device;
        hr = collection->Item(id, &device);
        if (FAILED(hr) || !device)
            continue;

        FpState err = GetDeviceDesc(device, desc);
        if (err)
            continue;
        descs.push_back(desc);
    }

    return descs;
}

FpState MMAudioPlayer::GetDeviceDesc(com_ptr<struct IMMDevice> device, MMDeviceDesc & desc)
{
    if (!device)
        return FpStateNullptr;

    HRESULT hr = S_OK;
    LPWSTR devid = NULL;
    hr = device->GetId(&devid);
    if (FAILED(hr))
        return FpStateInner;

    u8string strDevid = ws2u8(devid, -1);
    CoTaskMemFree(devid);

    com_ptr<IPropertyStore> props;
    hr = device->OpenPropertyStore(STGM_READ, &props);
    if (FAILED(hr) || !props)
        return FpStateInner;

    PROPVARIANT var;
    PropVariantInit(&var);
    hr = props->GetValue(PKEY_Device_FriendlyName, &var);
    if (FAILED(hr))
    {
        PropVariantClear(&var);
        return FpStateInner;
    }

    u8string strFriendlyName = ws2u8(var.pwszVal, -1);
    PropVariantClear(&var);

    desc.devid = strDevid;
    desc.friendlyName = strFriendlyName;

    //格式信息

    com_ptr<struct IAudioClient> audioClient;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, reinterpret_cast<void **>(&audioClient));
    if (FAILED(hr) || !audioClient)
        return FpStateInner;

    WAVEFORMATEX * waveformat = nullptr;
    audioClient->GetMixFormat(&waveformat);
    if (FAILED(hr) || !waveformat)
        return FpStateInner;

    desc.format.chanels = waveformat->nChannels;
    desc.format.sampleRate = waveformat->nSamplesPerSec;
    if ((waveformat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) && (waveformat->wBitsPerSample == 32))
        desc.format.sampleFormat = AV_SAMPLE_FMT_FLT;
    else if ((waveformat->wFormatTag == WAVE_FORMAT_PCM) && (waveformat->wBitsPerSample == 16))
        desc.format.sampleFormat = AV_SAMPLE_FMT_S16;
    else if ((waveformat->wFormatTag == WAVE_FORMAT_PCM) && (waveformat->wBitsPerSample == 32))
        desc.format.sampleFormat = AV_SAMPLE_FMT_S32;
    else if (waveformat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE * ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(waveformat);
        if (ext->SubFormat == SDL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && (waveformat->wBitsPerSample == 32))
            desc.format.sampleFormat = AV_SAMPLE_FMT_FLT;
        else if (ext->SubFormat == SDL_KSDATAFORMAT_SUBTYPE_PCM && (waveformat->wBitsPerSample == 16))
            desc.format.sampleFormat = AV_SAMPLE_FMT_S16;
        else if (ext->SubFormat == SDL_KSDATAFORMAT_SUBTYPE_PCM && (waveformat->wBitsPerSample == 32))
            desc.format.sampleFormat = AV_SAMPLE_FMT_S32;
        else {}
    }
    else
        desc.format.sampleFormat = AV_SAMPLE_FMT_NONE;
    CoTaskMemFree(waveformat);

    return FpStateOK;
}

FpState MMAudioPlayer::GetDefaultDeviceDesc(MMDeviceDesc & desc)
{
    HRESULT hr = S_OK;
    if (!_enumerator)
    {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&_enumerator));
        if (FAILED(hr) || !_enumerator)
            return FpStateGeneric;
    }

    com_ptr<IMMDevice> audioDevice;
    hr = _enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &audioDevice);
    if (FAILED(hr) || !audioDevice)
        return FpStateGeneric;
    return GetDeviceDesc(audioDevice, desc);
}

FpState MMAudioPlayer::Start()
{
    if (_thPlay.joinable())
        return FpStateOK;

    if (!_enumerator)
    {
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&_enumerator));
        if (FAILED(hr) || !_enumerator)
            return FpStateGeneric;
    }

    _thPlay = std::thread(std::bind(&MMAudioPlayer::playThread, this));

    return FpStateOK;
}

FpState MMAudioPlayer::WaitForStop()
{
    if (_thPlay.get_id() == std::thread::id())
        return FpStateOK;

    _thPlay.join();
    return FpStateOK;
}

std::tuple<FpState, std::shared_ptr<Clock>> MMAudioPlayer::AddAudio(std::shared_ptr<IAudioBufferInputStream> stream)
{
    std::lock_guard<std::mutex> lock(_mtx);
    std::shared_ptr<Clock> clock = std::make_shared<Clock>();
    _playItems.push_back({ stream, {}, FpStateOK, clock });
    return { FpStateOK, clock };
}

FpState MMAudioPlayer::resetDevice()
{
    _audioClient->Stop();
    _audioRenderClient.reset();
    _audioClient.reset();
    _audioDevice.reset();
    _state = initialDevice();
    if (_state < 0)
        return _state;
    return FpStateOK;
    //return _decoder->WaitForFrames(std::numeric_limits<uint32_t>::max());
}

FpState MMAudioPlayer::initialDevice()
{
    if (!_enumerator)
    {
        _state = FpStateNullptr;
        return FpStateNullptr;
    }

    HRESULT hr = S_OK;
    com_ptr<IMMDevice> audioDevice;
    hr = _enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &audioDevice);
    if (FAILED(hr) || !audioDevice)
        return FpStateGeneric;

    com_ptr<struct IAudioClient> audioClient;
    hr = audioDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, reinterpret_cast<void **>(&audioClient));
    if (FAILED(hr) || !audioClient)
        return FpStateGeneric;

    WAVEFORMATEX * __waveformat = nullptr;
    audioClient->GetMixFormat(&__waveformat);
    if (FAILED(hr) || !__waveformat)
        return FpStateGeneric;
    std::unique_ptr<WAVEFORMATEX, decltype(CoTaskMemFree)*> waveformat(__waveformat, CoTaskMemFree);

    int64_t durationDefault = REFTIMES_PER_SEC / 20;
    int64_t durationMin = REFTIMES_PER_SEC / 20;

    hr = audioClient->GetDevicePeriod(&durationDefault, &durationMin);
    if (FAILED(hr) || durationMin <= 0)
        return FpStateGeneric;

    uint32_t audioClientFlags = /*AUDCLNT_STREAMFLAGS_EVENTCALLBACK*/0;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, audioClientFlags, durationMin, 0, waveformat.get(), NULL);
    if (FAILED(hr))
        return FpStateGeneric;

    uint32_t numBufferedSamples = 0;
    hr = audioClient->GetBufferSize(&numBufferedSamples);
    if (FAILED(hr) || numBufferedSamples <= 0)
        return FpStateGeneric;

    com_ptr<struct IAudioRenderClient> audioRenderClient;
    hr = audioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&audioRenderClient));
    if (FAILED(hr) || !audioRenderClient)
        return FpStateGeneric;

    hr = audioClient->Start();
    if (FAILED(hr))
        return FpStateGeneric;

    AVSampleFormat sampleFormat = AV_SAMPLE_FMT_NONE;
    if ((waveformat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) && (waveformat->wBitsPerSample == 32))
        sampleFormat = AV_SAMPLE_FMT_FLT;
    else if ((waveformat->wFormatTag == WAVE_FORMAT_PCM) && (waveformat->wBitsPerSample == 16))
        sampleFormat = AV_SAMPLE_FMT_S16;
    else if ((waveformat->wFormatTag == WAVE_FORMAT_PCM) && (waveformat->wBitsPerSample == 32))
        sampleFormat = AV_SAMPLE_FMT_S32;
    else if (waveformat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE * ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(waveformat.get());
        if (ext->SubFormat == SDL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && (waveformat->wBitsPerSample == 32))
            sampleFormat = AV_SAMPLE_FMT_FLT;
        else if (ext->SubFormat == SDL_KSDATAFORMAT_SUBTYPE_PCM && (waveformat->wBitsPerSample == 16))
            sampleFormat = AV_SAMPLE_FMT_S16;
        else if (ext->SubFormat == SDL_KSDATAFORMAT_SUBTYPE_PCM && (waveformat->wBitsPerSample == 32))
            sampleFormat = AV_SAMPLE_FMT_S32;
        else {}
    }

    _numBufferedSamples = numBufferedSamples;
    _audioDevice = audioDevice;
    _audioClient = audioClient;
    _audioRenderClient = audioRenderClient;

    _format.chanels = waveformat->nChannels;
    _format.sampleRate = waveformat->nSamplesPerSec;
    _format.sampleFormat = sampleFormat;

    MMDeviceDesc deviceDesc = {};
    GetDeviceDesc(audioDevice, deviceDesc);
    std::cout << deviceDesc.friendlyName << ", "
        << deviceDesc.devid << ", "
        << av_get_sample_fmt_name(_format.sampleFormat) << ", "
        << _format.chanels << " chanels, "
        << _format.sampleRate << "Hz"
        << std::endl;
    return FpStateOK;
}

void MMAudioPlayer::playThread()
{
    thread_set_name(0, "playThread");


    _state = initialDevice();
    if (_state < 0)
        return;

    ////_state = _decoder->WaitForFrames(std::numeric_limits<uint32_t>::max());
    ////if (_state < 0)
    ////    return;

    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
    //::SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    HRESULT hr = S_OK;
    int64_t bytesPerSample = av_get_bytes_per_sample(_format.sampleFormat);
    int64_t bytesPerFrame = bytesPerSample * _format.chanels;

    double_t tsBase = get_time_hr();
    double_t ptsPlay = 0;
    bool needReset = false;
    std::list<playItemT> playItems;
    while(_state >= 0 && !(_flags & FpFlagStop))
    {
        {
            std::unique_lock<std::mutex> lock(_mtx, std::try_to_lock);
            if (lock.owns_lock())
            {
                for each(auto & item in _playItems)
                {
                    _state = item.stream->SetOutputFormat(_format);
                    if (_state < 0)
                        break;
                }
                playItems.splice(playItems.end(), _playItems);
            }
        }

        if (playItems.empty())
            break;

        auto iter = playItems.begin();
        while(iter != playItems.end())
        {
            if (iter->state < 0)
                iter = playItems.erase(iter);
            else
                ++iter;
        }

        int64_t numSamplesTotal = _numBufferedSamples / 2;
        uint8_t * mmBuffer = nullptr;
        hr = _audioRenderClient->GetBuffer(numSamplesTotal, &mmBuffer);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
        {
            resetDevice();
            needReset = true;
            continue;
        }

        if (playItems.size() > 1)
            memset(mmBuffer, 0, numSamplesTotal * bytesPerFrame);


        uint32_t numSamplesPaddingNow = 0;
        _audioClient->GetCurrentPadding(&numSamplesPaddingNow);
        for(auto & item : playItems)
        {
            if (needReset)
            {
                item.buffer = {};
                item.stream->SetOutputFormat(_format);
            }

            {
                int64_t numSamplesRenderd = 0;
                while (numSamplesRenderd < numSamplesTotal && item.state >= 0)
                {
                    int64_t numSamplesNeeded = numSamplesTotal - numSamplesRenderd;
                    int64_t numSamplesHave = item.buffer.numSamples - item.buffer.numSamplesRead;
                    if (numSamplesHave <= 0)
                    {
                        item.buffer = {};
                        item.state = item.stream->PeekBuffer(item.buffer);
                        //FpState state = _decoder->NextBuffer(buffer, std::numeric_limits<uint32_t>::max());
                        if (item.state == FpStatePending)
                        {
                            printf("stream is pending <%lld>...\n", _pendingCountTotal++);
                            break;
                        }

                        if (item.state < 0)
                            break;

                        numSamplesHave = item.buffer.numSamples;
                    }

                    int64_t numSamples = numSamplesHave < numSamplesNeeded ? numSamplesHave : numSamplesNeeded;
                    if (numSamples > 0)
                    {
                        uint8_t * dst = mmBuffer + (numSamplesRenderd * bytesPerFrame);
                        uint8_t * src = item.buffer.data.get() + (item.buffer.numSamplesRead * bytesPerFrame);
                        if (playItems.size() < 2)
                        {
                            memcpy(dst, src, numSamples * bytesPerFrame);

                            //for (uint32_t isam = 0; isam < numSamples * _format.chanels / 2; ++isam)
                            //{
                            //    float_t * dstF = (float_t *)(dst + isam * bytesPerSample);
                            //    float_t * srcF = (float_t *)(src + isam * bytesPerSample);
                            //    *dstF = *srcF;
                            //}
                        }
                        else
                        {
                            for (int64_t isam = 0; isam < numSamples * _format.chanels; ++isam)
                            {
                                float_t * dstF = (float_t *)(dst + isam * bytesPerSample);
                                float_t * srcF = (float_t *)(src + isam * bytesPerSample);
                                *dstF += *srcF;
                            }
                        }
                        numSamplesRenderd += numSamples;
                        item.buffer.numSamplesRead += numSamples;
                        item.numSamplesRendered += numSamples;
                    }
                }

                //for (uint32_t isam = 0; isam < numSamplesTotal * _format.chanels; ++isam)
                //{
                //    float_t * dstF = (float_t *)(mmBuffer + isam * bytesPerSample);
                //    *dstF /= playItems.size();
                //}
            }

            item.clock->pts = (item.numSamplesRendered - (_numBufferedSamples - numSamplesPaddingNow))/ (double)_format.sampleRate;
        }

        needReset = false;

        if (!mmBuffer)
            continue;

        hr = _audioRenderClient->ReleaseBuffer(numSamplesTotal, 0);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
        {
            resetDevice();
            needReset = true;
            continue;
        }

        if(FAILED(hr))
        {
            printf("error ReleaseBuffer!");
            break;
        }

        uint32_t numSamplesPadding = 0;
        while (_state >= 0 && !(_flags & FpFlagStop))
        {
            hr = _audioClient->GetCurrentPadding(&numSamplesPadding);
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
            {
                resetDevice();
                needReset = true;
                continue;
            }
            if (FAILED(hr))
            {
                _state = FpStateGeneric;
                break;
            }

            if (numSamplesPadding <= numSamplesTotal)
                break;

            Sleep(((numSamplesPadding - numSamplesTotal) * 1000) / _format.sampleRate);
        }
    }
}
