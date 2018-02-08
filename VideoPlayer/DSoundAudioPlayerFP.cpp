#include "stdafx.h"
#include "DSoundAudioPlayerFP.h"

//#include <dsound.h>
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


DSoundAudioPlayerFP::DSoundAudioPlayerFP(std::shared_ptr<IAudioDecoderFP> decoder)
    : _decoder(decoder)
{
}


DSoundAudioPlayerFP::~DSoundAudioPlayerFP()
{
    if(_audioEvent)
    {
        CloseHandle(reinterpret_cast<HANDLE>(_audioEvent));
        _audioEvent = 0;
    }
}

std::vector<MMDeviceDesc> DSoundAudioPlayerFP::GetDeviceDescs()
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

        FpError err = GetDeviceDesc(device, desc);
        if (err)
            continue;

        descs.push_back(desc);
    }

    return descs;
}

FpError DSoundAudioPlayerFP::GetDeviceDesc(com_ptr<struct IMMDevice> device, MMDeviceDesc & desc)
{
    if (!device)
        return FpErrorNullptr;

    HRESULT hr = S_OK;
    LPWSTR devid = NULL;
    hr = device->GetId(&devid);
    if (FAILED(hr))
        return FpErrorInner;

    u8string strDevid = ws2u8(devid, -1);
    CoTaskMemFree(devid);

    com_ptr<IPropertyStore> props;
    hr = device->OpenPropertyStore(STGM_READ, &props);
    if (FAILED(hr) || !props)
        return FpErrorInner;

    PROPVARIANT var;
    PropVariantInit(&var);
    hr = props->GetValue(PKEY_Device_FriendlyName, &var);
    if (FAILED(hr))
    {
        PropVariantClear(&var);
        return FpErrorInner;
    }

    u8string strFriendlyName = ws2u8(var.pwszVal, -1);
    PropVariantClear(&var);

    desc.devid = strDevid;
    desc.friendlyName = strFriendlyName;
    return FpErrorOK;
}

FpError DSoundAudioPlayerFP::Start()
{
    if (!_decoder)
        return FpErrorBadData;

    if (_thPlay.joinable())
        return FpErrorOK;

    HRESULT hr = S_OK;

    if (!_enumerator)
    {
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&_enumerator));
        if (FAILED(hr) || !_enumerator)
            return FpErrorGeneric;
    }

    com_ptr<IMMDevice> audioDevice;
    hr = _enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &audioDevice);
    if (FAILED(hr) || !audioDevice)
        return FpErrorGeneric;

    MMDeviceDesc deviceDesc = {};
    GetDeviceDesc(audioDevice, deviceDesc);
    printf("use %s <%s>.\n", deviceDesc.friendlyName.c_str(), deviceDesc.devid.c_str());

    com_ptr<struct IAudioClient> audioClient;
    hr = audioDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, reinterpret_cast<void **>(&audioClient));
    if (FAILED(hr) || !audioClient)
        return FpErrorGeneric;

    WAVEFORMATEX * __waveformat = nullptr;
    audioClient->GetMixFormat(&__waveformat);
    if (FAILED(hr) || !__waveformat)
        return FpErrorGeneric;
    std::unique_ptr<WAVEFORMATEX, decltype(CoTaskMemFree)*> waveformat(__waveformat, CoTaskMemFree);

    int64_t durationDefault = REFTIMES_PER_SEC / 20;
    int64_t durationMin = REFTIMES_PER_SEC / 20;
    int64_t durationUse = REFTIMES_PER_SEC / 20;

    hr = audioClient->GetDevicePeriod(&durationDefault, &durationMin);
    if (FAILED(hr) || durationMin <= 0)
        return FpErrorGeneric;

    uint32_t audioClientFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, audioClientFlags, durationMin, 0, waveformat.get(), NULL);
    if (FAILED(hr))
        return FpErrorGeneric;

    uint32_t numBufferedSamples = 0;
    hr = audioClient->GetBufferSize(&numBufferedSamples);
    if (FAILED(hr) || numBufferedSamples <= 0)
        return FpErrorGeneric;

    HANDLE audioEvent = NULL;
    if(audioClientFlags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK)
    {
        audioEvent = CreateEventExW(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        //HANDLE audioEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!audioEvent)
            return FpErrorGeneric;
        hr = audioClient->SetEventHandle(audioEvent);
        if (FAILED(hr))
        {
            CloseHandle(audioEvent);
            return FpErrorGeneric;
        }
    }

    com_ptr<struct IAudioRenderClient> audioRenderClient;
    hr = audioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&audioRenderClient));
    if (FAILED(hr) || !audioRenderClient)
    {
        if(audioEvent)
            CloseHandle(audioEvent);
        return FpErrorGeneric;
    }

    hr = audioClient->Start();
    if (FAILED(hr))
    {
        if (audioEvent)
            CloseHandle(audioEvent);
        return FpErrorGeneric;
    }

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
    double bufferDuration = (double)_numBufferedSamples / waveformat->nSamplesPerSec;
    _audioDevice = audioDevice;
    _audioClient = audioClient;
    _audioRenderClient = audioRenderClient;
    _audioEvent = (intmax_t)audioEvent;

    //BYTE * pData = nullptr;
    //hr = audioRenderClient->GetBuffer(numBufferedSamples, &pData);
    //hr = audioRenderClient->ReleaseBuffer(numBufferedSamples, 0);
    //hr = audioRenderClient->GetBuffer(numBufferedSamples, &pData);
    //hr = audioRenderClient->ReleaseBuffer(numBufferedSamples, 0);

    _format.chanels = waveformat->nChannels;
    _format.sampleRate = waveformat->nSamplesPerSec;
    _format.sampleFormat = sampleFormat;
    _format.bits = waveformat->wBitsPerSample;
    _format.numBufferedSamples = _numBufferedSamples;
    FpError err = _decoder->ResetFormat(_format);
    if (err)
        return FpErrorGeneric;

    _thPlay = std::thread(std::bind(&DSoundAudioPlayerFP::playThread, this));
#if 0
    {
        int averr = 0;
        HRESULT hr = S_OK;
        HANDLE audioEvent = (HANDLE)_audioEvent;
        while (true)
        {
            uint32_t wait = ::WaitForSingleObject(audioEvent, INFINITE);
            if (wait == WAIT_OBJECT_0)
            {
                BYTE * pData = nullptr;
                uint32_t numBufferedSamplesTotal = _numBufferedSamples;
                uint32_t numBufferedSamples = 0;
                hr = _audioRenderClient->GetBuffer(numBufferedSamplesTotal, &pData);
                if (!pData)
                    continue;

                while (numBufferedSamples < numBufferedSamplesTotal)
                {
                    uint32_t numNeeded = numBufferedSamplesTotal - numBufferedSamples;

                    if (!_swr)
                    {
                        FpAudioFormat _inputFormat = _decoder->GetOutputFormat();
                        {
                            int64_t in_channel_layout = av_get_default_channel_layout(_inputFormat.chanels);
                            int64_t out_channel_layout = av_get_default_channel_layout(_format.chanels);

#if LIBSWRESAMPLE_VERSION_MAJOR >= 3
                            //_swr.reset(swr_alloc(), swr_free_wrap);

                            //av_opt_set_int(_swr.get(), "in_channel_layout", in_channel_layout, 0);
                            //av_opt_set_int(_swr.get(), "in_sample_rate", _inputFormat.sampleRate, 0);
                            //av_opt_set_sample_fmt(_swr.get(), "in_sample_fmt", _inputFormat.sampleFormat, 0);

                            //av_opt_set_int(_swr.get(), "out_channel_layout", out_channel_layout, 0);
                            //av_opt_set_int(_swr.get(), "out_sample_rate", _format.sampleRate, 0);
                            //av_opt_set_sample_fmt(_swr.get(), "out_sample_fmt", _format.sampleFormat, 0);

                            _swr.reset(swr_alloc_set_opts(nullptr, out_channel_layout, _format.sampleFormat, _format.sampleRate,
                                in_channel_layout, _inputFormat.sampleFormat, _inputFormat.sampleRate, 0, NULL), swr_free_wrap);
#else
                            _swr.reset(swr_alloc_set_opts(nullptr, out_channel_layout, _format.sampleFormat, _format.sampleRate,
                                in_channel_layout, _inputFormat.sampleFormat, _inputFormat.sampleRate, 0, NULL), swr_free_wrap);
#endif
                            averr = swr_init(_swr.get());
                            if (averr < 0)
                                break;
                        }
                    }

                    uint8_t * data = pData + numBufferedSamples * av_get_bytes_per_sample(_format.sampleFormat) * _format.chanels;
                    averr = swr_convert(_swr.get(), &data, numNeeded, 0, 0);
                    if (averr < 0)
                        break;

                    if (averr == 0)
                    {
                        _lastFrame = _decoder->NextFrame();
                        if (!_lastFrame.ptr)
                            break;

                        printf("frame %lld\n", _lastFrame.index);

                        averr = swr_convert(_swr.get(), &data, numNeeded, (const uint8_t**)_lastFrame.ptr->data, _lastFrame.ptr->nb_samples);
                        if (averr < 0)
                            break;
                    }

                    if (averr > 0)
                    {
                        numBufferedSamples += averr;
                    }
                }

                hr = _audioRenderClient->ReleaseBuffer(numBufferedSamples, 0);
                if (FAILED(hr))
                {
                    printf("error ReleaseBuffer!");
                    break;
                }
            }
        }
    }
#endif
    return FpErrorOK;
}

FpError DSoundAudioPlayerFP::WaitForStop()
{
    _thPlay.join();
    return FpErrorNotNow;
}

void DSoundAudioPlayerFP::playThread()
{
    thread_set_name(0, "playThread");

    if (!_decoder || !_audioRenderClient)
        return;

    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
    //::SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    int averr = 0;
    HRESULT hr = S_OK;
    HANDLE audioEvent = (HANDLE)_audioEvent;
    uint32_t bytesPerSample = av_get_bytes_per_sample(_format.sampleFormat) * _format.chanels;

    FpAudioBuffer buffer;
    uint32_t numBuffersPlayed = 0;
    while(_state >= 0)
    {
        uint32_t numSamplesTotal = _numBufferedSamples / _numBufferedSamplesCount;
        uint8_t * mmBuffer = nullptr;
        hr = _audioRenderClient->GetBuffer(numSamplesTotal, &mmBuffer);
        if (!mmBuffer)
            continue;

        uint32_t numSamplesRenderd = 0;
        while(numSamplesRenderd < numSamplesTotal && _state >= 0)
        {
            uint32_t numSamplesNeeded = numSamplesTotal - numSamplesRenderd;
            uint32_t numSamplesHave = buffer.numSamples - numBuffersPlayed;
            if(numSamplesHave == 0)
            {
                numBuffersPlayed = 0;
                buffer = _decoder->NextBuffer();
                if(buffer.numSamples == 0)
                {
                    _state = FpErrorPending;
                    break;
                }
                numSamplesHave = buffer.numSamples;
            }
            uint32_t numSamples = numSamplesHave < numSamplesNeeded ? numSamplesHave : numSamplesNeeded;
            if(numSamples > 0)
            {
                memcpy(mmBuffer + (numSamplesRenderd * bytesPerSample), buffer.data.get() + (numBuffersPlayed * bytesPerSample), numSamples * bytesPerSample);
                numSamplesRenderd += numSamples;
                numBuffersPlayed += numSamples;
            }
        }

        hr = _audioRenderClient->ReleaseBuffer(numSamplesRenderd, 0);
        if(FAILED(hr))
        {
            printf("error ReleaseBuffer!");
            break;
        }

        uint32_t numSamplesPadding = 0;
        while (_state >= 0)
        {
            hr = _audioClient->GetCurrentPadding(&numSamplesPadding);
            if (FAILED(hr))
            {
                _state = FpErrorGeneric;
                break;
            }

            if (numSamplesPadding <= numSamplesTotal)
                break;

            Sleep(((numSamplesPadding - numSamplesTotal) * 1000) / _format.sampleRate);
        }
    }

}
