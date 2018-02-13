#pragma once
#include "IMedia.h"

class FFmpegVideoDecoder : public IVideoDecoder
{
public:
    FFmpegVideoDecoder(std::shared_ptr<IVideoPacketReader> reader);
    ~FFmpegVideoDecoder();

    std::shared_ptr<IVideoPacketReader> Stream() const;
    VideoFormat GetOutputFormat() const;
    FpState SetOutputFormat(VideoFormat format);

    FpState Ready(int64_t timeoutMS);
    FpState WaitForFrames(int64_t timeoutMS);
    FpState PeekBuffer(VideoBuffer & buffer);
    FpState NextBuffer(VideoBuffer & buffer, int64_t timeoutMS);

private:
    FpState decodeFrame(std::shared_ptr<AVFrame> avframe, VideoBuffer & buffer);
    FpState readFrame(std::shared_ptr<AVFrame> & frame);
    void decoderThread();

private:
    std::shared_ptr<IVideoPacketReader> _reader;

    VideoDecodeParam _inputParam;
    VideoFormat _outputFormat;

    std::shared_ptr<AVCodecContext> _codec;
    std::shared_ptr<SwsContext> _sws;

    std::list<VideoBuffer> _buffers{};

    std::atomic<int64_t> _sessionIndex = 0;
    std::list<std::shared_ptr<AVFrame>> _sessionFrames{};

    std::mutex _mtx;
    std::thread _thDecoder;
    std::condition_variable _condRead;
    std::condition_variable _condDecoder;
    std::atomic<FpState> _state = FpStateOK;
    std::atomic<int32_t> _flags = FpFlagNone;

    int32_t _minFrames = 4;
    int32_t _asyncTimeOutTime = 5000;
};
