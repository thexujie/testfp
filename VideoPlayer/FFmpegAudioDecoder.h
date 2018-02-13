#pragma once
#include "IMedia.h"

class FFmpegAudioDecoder : public IAudioDecoder
{
public:
    FFmpegAudioDecoder(std::shared_ptr<IAudioPacketReader> reader);
    ~FFmpegAudioDecoder();

    std::shared_ptr<IAudioPacketReader> Stream() const;
    AudioFormat GetOutputFormat() const;
    FpState SetOutputFormat(AudioFormat format);

    FpState Ready(int64_t timeoutMS);
    FpState WaitForFrames(int64_t timeoutMS);
    FpState PeekBuffer(AudioBuffer & buffer);
    FpState NextBuffer(AudioBuffer & buffer, int64_t timeoutMS);

private:
    FpState decodeFrame(std::shared_ptr<AVFrame> avframe, AudioBuffer & buffer);
    FpState readFrame(std::shared_ptr<AVFrame> & frame);
    void decoderThread();

private:
    std::shared_ptr<IAudioPacketReader> _reader;

    AudioDecodeParam _inputParam;
    AudioFormat _outputFormat;

    std::shared_ptr<AVCodecContext> _codec;
    std::shared_ptr<SwrContext> _swr;

    std::list<AudioBuffer> _buffers;

    std::atomic<int64_t> _sessionIndex = 0;
    std::list<std::shared_ptr<AVFrame>> _sessionFrames;

    std::mutex _mtx;
    std::thread _thDecoder;
    std::condition_variable _condRead;
    std::condition_variable _condDecoder;
    std::atomic<FpState> _state = FpStateOK;
    std::atomic<int32_t> _flags = FpFlagNone;

    int32_t _minFrames = 64;
    int32_t _maxFrames = 128;
    int32_t _asyncTimeOutTime = 5000;
};
