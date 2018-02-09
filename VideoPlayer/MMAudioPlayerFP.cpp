#include "stdafx.h"
#include "MMAudioPlayerFP.h"

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


MMAudioPlayerFP::MMAudioPlayerFP(std::shared_ptr<IAudioDecoderFP> decoder)
    : _decoder(decoder)
{
}


MMAudioPlayerFP::~MMAudioPlayerFP()
{
    if(_audioEvent)
    {
        CloseHandle(reinterpret_cast<HANDLE>(_audioEvent));
        _audioEvent = 0;
    }
}

std::vector<MMDeviceDesc> MMAudioPlayerFP::GetDeviceDescs()
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

FpState MMAudioPlayerFP::GetDeviceDesc(com_ptr<struct IMMDevice> device, MMDeviceDesc & desc)
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
    return FpStateOK;
}

FpState MMAudioPlayerFP::Start()
{
    if (!_decoder)
        return FpStateBadData;

    if (_thPlay.joinable())
        return FpStateOK;

    if (!_enumerator)
    {
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&_enumerator));
        if (FAILED(hr) || !_enumerator)
            return FpStateGeneric;
    }

    _thPlay = std::thread(std::bind(&MMAudioPlayerFP::playThread, this));

    return FpStateOK;
}

FpState MMAudioPlayerFP::WaitForStop()
{
    if (_thPlay.get_id() == std::thread::id())
        return FpStateOK;

    _thPlay.join();
    return FpStateNotNow;
}

FpState MMAudioPlayerFP::resetDevice()
{
    _audioClient->Stop();
    _audioRenderClient.reset();
    _audioClient.reset();
    _audioDevice.reset();
    _state = initialDevice();
    if (_state < 0)
        return _state;
    return _decoder->WaitForFrames(std::numeric_limits<uint32_t>::max());
}

FpState MMAudioPlayerFP::initialDevice()
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

    uint32_t audioClientFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, audioClientFlags, durationMin, 0, waveformat.get(), NULL);
    if (FAILED(hr))
        return FpStateGeneric;

    uint32_t numBufferedSamples = 0;
    hr = audioClient->GetBufferSize(&numBufferedSamples);
    if (FAILED(hr) || numBufferedSamples <= 0)
        return FpStateGeneric;

    HANDLE audioEvent = NULL;
    if (audioClientFlags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK)
    {
        audioEvent = CreateEventExW(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        //HANDLE audioEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!audioEvent)
            return FpStateGeneric;
        hr = audioClient->SetEventHandle(audioEvent);
        if (FAILED(hr))
        {
            CloseHandle(audioEvent);
            return FpStateGeneric;
        }
    }

    com_ptr<struct IAudioRenderClient> audioRenderClient;
    hr = audioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&audioRenderClient));
    if (FAILED(hr) || !audioRenderClient)
    {
        if (audioEvent)
            CloseHandle(audioEvent);
        return FpStateGeneric;
    }

    hr = audioClient->Start();
    if (FAILED(hr))
    {
        if (audioEvent)
            CloseHandle(audioEvent);
        return FpStateGeneric;
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

    _format.chanels = waveformat->nChannels;
    _format.sampleRate = waveformat->nSamplesPerSec;
    _format.sampleFormat = sampleFormat;
    _format.bits = waveformat->wBitsPerSample;
    _format.numBufferedSamples = _numBufferedSamples;
    FpState err = _decoder->SetOutputFormat(_format);
    if (err < 0)
        return err;

    MMDeviceDesc deviceDesc = {};
    GetDeviceDesc(audioDevice, deviceDesc);
    printf("%s <%s> chanels=%d, samples=%d, format=%s.\n", 
        deviceDesc.friendlyName.c_str(), deviceDesc.devid.c_str(),
        _format.chanels, _format.sampleRate, av_get_sample_fmt_name(_format.sampleFormat));
    return FpStateOK;
}

void MMAudioPlayerFP::playThread()
{
    thread_set_name(0, "playThread");

    if (!_decoder)
        return;

    _state = initialDevice();
    if (_state < 0)
        return;

    _state = _decoder->WaitForFrames(std::numeric_limits<uint32_t>::max());
    if (_state < 0)
        return;

    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
    //::SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    int averr = 0;
    HRESULT hr = S_OK;
    HANDLE audioEvent = (HANDLE)_audioEvent;
    uint32_t bytesPerSample = av_get_bytes_per_sample(_format.sampleFormat) * _format.chanels;

    FpAudioBuffer buffer;
    while(_state >= 0)
    {
        uint32_t numSamplesTotal = _numBufferedSamples / _numBufferedSamplesCount;
        uint8_t * mmBuffer = nullptr;
        hr = _audioRenderClient->GetBuffer(numSamplesTotal, &mmBuffer);
        if(hr == AUDCLNT_E_DEVICE_INVALIDATED)
        {
            buffer = {};
            resetDevice();
            printf("new device ...\n");
            continue;
        }

        if (!mmBuffer)
            continue;

        uint32_t numSamplesRenderd = 0;
        while(numSamplesRenderd < numSamplesTotal && _state >= 0)
        {
            uint32_t numSamplesNeeded = numSamplesTotal - numSamplesRenderd;
            uint32_t numSamplesHave = buffer.numSamples - buffer.numSamplesRead;
            if(numSamplesHave == 0)
            {
                buffer = {};
                _state = _decoder->PeekBuffer(buffer);
                //FpState state = _decoder->NextBuffer(buffer, std::numeric_limits<uint32_t>::max());
                if (_state == FpStatePending || buffer.numSamples == 0)
                {
                    _state = FpStatePending;
                    static int pending = 0;
                    printf("stream is pending <%d>...\n", pending++);
                    break;
                }

                if(_state < 0)
                    break;
                numSamplesHave = buffer.numSamples;
            }
            uint32_t numSamples = numSamplesHave < numSamplesNeeded ? numSamplesHave : numSamplesNeeded;
            if(numSamples > 0)
            {
                memcpy(mmBuffer + (numSamplesRenderd * bytesPerSample), buffer.data.get() + (buffer.numSamplesRead * bytesPerSample), numSamples * bytesPerSample);
                numSamplesRenderd += numSamples;
                buffer.numSamplesRead += numSamples;
            }
        }

        if(numSamplesRenderd < numSamplesTotal)
        {
            uint32_t numSamples = numSamplesTotal - numSamplesRenderd;
            memset(mmBuffer + (numSamplesRenderd * bytesPerSample), 0, numSamples * bytesPerSample);
            numSamplesRenderd += numSamples;
        }
        hr = _audioRenderClient->ReleaseBuffer(numSamplesRenderd, 0);
        if(hr == AUDCLNT_E_DEVICE_INVALIDATED)
        {
            buffer = {};
            resetDevice();
            printf("new device ...\n");
            continue;
        }

        if(FAILED(hr))
        {
            printf("error ReleaseBuffer!");
            break;
        }

        uint32_t numSamplesPadding = 0;
        while (_state >= 0)
        {
            hr = _audioClient->GetCurrentPadding(&numSamplesPadding);
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
            {
                buffer = {};
                resetDevice();
                break;
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
