#pragma once

#include "IMedia.h"

struct MMDeviceDesc
{
    u8string devid;
    u8string friendlyName;
};

class MMAudioPlayerFP : public IAudioPlayerFP
{
public:
    MMAudioPlayerFP(std::shared_ptr<IAudioDecoderFP> decoder);
    ~MMAudioPlayerFP();

    std::vector<MMDeviceDesc> GetDeviceDescs();
    FpState GetDeviceDesc(com_ptr<struct IMMDevice> device, MMDeviceDesc & desc);
    FpState Start();
    FpState WaitForStop();
private:
    FpState resetDevice();
    FpState initialDevice();
    void playThread();

private:
    std::shared_ptr<IAudioDecoderFP> _decoder;
    std::thread _thPlay;

    com_ptr<struct IMMDeviceEnumerator> _enumerator;
    com_ptr<struct IMMDevice> _audioDevice;
    com_ptr<struct IAudioClient> _audioClient;
    com_ptr<struct IAudioRenderClient> _audioRenderClient;

    uint32_t _numBufferedSamples = 0;
    std::vector<uint8_t> _audioBuffers;
    FpAudioFormat _format;

    std::atomic<FpState> _state = FpStateOK;

    std::shared_ptr<SwrContext> _swr;

    int64_t _pendingCountTotal = 0;
};
