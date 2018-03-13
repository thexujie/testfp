#pragma once
#include "IMedia.h"

class FFmpegVideoDecoder : public IVideoDecoder
{
public:
    FFmpegVideoDecoder(std::shared_ptr<IVideoPacketReader> reader);
    ~FFmpegVideoDecoder();

    FpState SetVideoFormatCooperator(std::shared_ptr<IVideoDecoderHWAccelerator> formatCooperator);
    FpState SetOutputFormat(VideoFormat format);

    std::shared_ptr<IVideoPacketReader> Stream() const;
    VideoFormat GetOutputFormat() const;
    VideoFormat DecodeFormat() const;
    VideoFormat OutputFormat() const;

    FpState Ready(int64_t timeoutMS);
    FpState WaitForFrames(int64_t timeoutMS);
    FpState PeekBuffer(VideoBuffer & buffer);
    FpState NextBuffer(VideoBuffer & buffer, int64_t timeoutMS);
    std::tuple<int64_t, int64_t> BufferQuality() const;

private:
    static int32_t _hwaccelGetBuffer(AVCodecContext *codecCtx, AVFrame *frame, int32_t flags);
    static AVPixelFormat _cooperateFormat(AVCodecContext * codecContext, const AVPixelFormat * pixelFormats);
    AVPixelFormat cooperateFormat(AVCodecContext * codecContext, const AVPixelFormat * pixelFormats);
    int32_t hwaccelGetBuffer(AVCodecContext * codecContext, AVFrame * frame, int32_t flags) const;
    FpState initDecoder();
    FpState scaleFrame(std::shared_ptr<AVFrame> avframe, VideoBuffer & buffer);
    FpState decodeFrame(std::shared_ptr<AVFrame> & frame);
    void decoderThread();

private:
    std::shared_ptr<IVideoPacketReader> _reader;

    //输入参数
    VideoParam _inputParam;
    //解码出的原始格式
    VideoFormat _decodeFormat;
    //输出格式（可能需要处理）
    VideoFormat _outputFormat;

    // 硬件加速
    std::shared_ptr<IVideoDecoderHWAccelerator> _hwAccel;
    std::shared_ptr<IVideoDecoderHWAccelContext> _hwAccellerator;
    std::shared_ptr<AVBufferRef> _hwDeviceContext;
    AVHWDeviceType _hwAccelDeviceTypeDefault = AV_HWDEVICE_TYPE_NONE;
    AVHWDeviceType _hwAccelDeviceType = AV_HWDEVICE_TYPE_NONE;
    AVPixelFormat _hwAccelPixelFormat = AV_PIX_FMT_NONE;


    std::shared_ptr<AVCodecContext> _codecContext;
    std::shared_ptr<SwsContext> _sws;

    std::list<VideoBuffer> _buffers{};

    std::atomic<int64_t> _sessionIndex = 0;
	std::list<std::shared_ptr<AVFrame>> _sessionFrames;
	std::list<Packet> _groupedPackets;
	std::list<Packet> _sessionPackets;

    std::mutex _mtx;
    std::thread _thDecoder;
    std::condition_variable _condRead;
    std::condition_variable _condDecoder;
    std::atomic<FpState> _state = FpStateOK;
    std::atomic<int32_t> _flags = FpFlagNone;

	std::atomic<int64_t> _dts = 0;
    int64_t _currPacketIndex = 0;
    int64_t _index = 0;
    int32_t _maxFrames = 4;
    int32_t _asyncTimeOutTime = 5000;
};
