#pragma once

#include "IMedia.h"

struct MMDeviceDesc
{
    u8string devid;
    u8string friendlyName;
    AudioFormat format;
};

class MMAudioPlayer : public IAudioPlayer
{
public:
    MMAudioPlayer();
    ~MMAudioPlayer();

    std::vector<MMDeviceDesc> GetDeviceDescs();
    FpState GetDeviceDesc(com_ptr<struct IMMDevice> device, MMDeviceDesc & desc);
    FpState GetDefaultDeviceDesc(MMDeviceDesc & desc);
    FpState Start();
    FpState Stop();
    FpState WaitForStop();

    std::tuple<FpState, std::shared_ptr<Clock>> AddAudio(std::shared_ptr<IAudioBufferInputStream> stream);

    std::shared_ptr<Clock> GetClock();

private:
    FpState resetDevice();
    FpState initialDevice();
    void playThread();

private:
    std::thread _thPlay;

    struct playItemT
    {
        int64_t index = 0;
        std::shared_ptr<IAudioBufferInputStream> stream;
        std::shared_ptr<Clock> clock;

        AudioBuffer buffer;
        FpState state = FpStateOK;

        int64_t numSamplesRendered = 0;
        double_t ptsRendered = 0;
    };

    std::mutex _mtx;

    int64_t _itemIndex = 0;
    std::list<playItemT> _playItems;

    com_ptr<struct IMMDeviceEnumerator> _enumerator;
    com_ptr<struct IMMDevice> _audioDevice;
    com_ptr<struct IAudioClient> _audioClient;
    com_ptr<struct IAudioRenderClient> _audioRenderClient;

    int64_t _numBufferedSamples = 0;
    std::vector<uint8_t> _audioBuffers;
    AudioFormat _format;

    std::atomic<FpState> _state = FpStateOK;
    std::atomic<int32_t> _flags = FpFlagNone;
    std::atomic<double_t> _pts = 0;

    std::shared_ptr<SwrContext> _swr;

    int64_t _pendingCountTotal = 0;
};
