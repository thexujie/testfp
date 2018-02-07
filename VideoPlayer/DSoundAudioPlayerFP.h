#pragma once

#include "IMedia.h"

struct MMDeviceDesc
{
    u8string devid;
    u8string friendlyName;
};

class DSoundAudioPlayerFP : public IAudioPlayerFP
{
public:
    DSoundAudioPlayerFP(std::shared_ptr<IAudioDecoderFP> decoder);
    ~DSoundAudioPlayerFP();

    std::vector<MMDeviceDesc> GetDeviceDescs();
    FpError GetDeviceDesc(com_ptr<struct IMMDevice> device, MMDeviceDesc & desc);
    FpError Start();
    FpError WaitForStop();
private:
    void playThread();
private:
    std::shared_ptr<IAudioDecoderFP> _decoder;
    std::thread _thPlay;

    com_ptr<struct IMMDeviceEnumerator> _enumerator;
    com_ptr<struct IMMDevice> _audioDevice;
    com_ptr<struct IAudioClient> _audioClient;
    com_ptr<struct IAudioRenderClient> _audioRenderClient;
    intmax_t _audioEvent = 0;
    uint32_t _numBufferedSamples = 0;
    std::vector<uint8_t> _audioBuffers;
    FpAudioFormat _format;
    FpFrame _lastFrame;

    std::shared_ptr<SwrContext> _swr;
};
