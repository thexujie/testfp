#pragma once

#include <map>
#include <memory>
#include <map>
#include <queue>
#include <functional>
#include <mutex>
#include <atomic>
#include "com_ptr.h"

const int32_t TIME_BASE_S = AV_TIME_BASE;
const int32_t TIME_BASE_MS = AV_TIME_BASE / 1000;

#include <memory>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
#include <libavutil/imgutils.h>
#include "libavutil/avutil.h"
#include <libavutil/opt.h>
#include "libavutil/time.h"
}

enum CodecLevel
{
	CodecLevelNone,
	CodecLevelSD,
	CodecLevelHD,
	CodecLevelFHD,
	CodecLevel4K,
	CodecLevel8K,
};

struct Clock
{
    std::atomic<double_t> pts;
};

struct Packet
{
    std::shared_ptr<AVPacket> ptr;
    int64_t index;
    int64_t localIndex;
};

struct AudioBuffer
{
    int64_t index = 0;
    std::shared_ptr<AVFrame> avframe;
    std::shared_ptr<uint8_t> data;
    double_t pts = 0;

    // 每通道样本数
    int64_t numSamples = 0;
};

struct VideoBuffer
{
    static const int32_t MAX_PLANE = 8;
    int64_t index = 0;
    int64_t width = 0;
    int64_t height = 0;
    std::shared_ptr<AVFrame> avframe;

    double_t pts = 0;
    double_t spf = 0;
    double_t dtc = 0;
};

struct AudioFormat
{
    int32_t chanels = 0;
    int32_t sampleRate = 0;
    AVSampleFormat sampleFormat = AV_SAMPLE_FMT_NONE;
};
inline bool operator==(const AudioFormat & lhs, const AudioFormat & rhs)
{
    return lhs.chanels == rhs.chanels &&
        lhs.sampleRate == rhs.sampleRate &&
        lhs.sampleFormat == rhs.sampleFormat;
}

inline bool operator!=(const AudioFormat & lhs, const AudioFormat & rhs)
{
    return !(lhs == rhs);
}

struct VideoFormat
{
    int32_t width = 0;
    int32_t height = 0;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
};

inline bool operator==(const VideoFormat & lhs, const VideoFormat & rhs)
{
    return lhs.width == rhs.width &&
        lhs.height == rhs.height &&
        lhs.pixelFormat == rhs.pixelFormat;
}

inline bool operator!=(const VideoFormat & lhs, const VideoFormat & rhs)
{
    return !(lhs == rhs);
}

struct AudioParam
{
    AVCodecID codecId = AV_CODEC_ID_NONE;
    uint32_t codecTag = 0;
    AudioFormat format{};

    int64_t bitRate = 0;
    int32_t bitsPerCodedSample = 0;
    int32_t bitsPerRawSample = 0;
    int32_t profile = 0;
    int32_t level = 0;

    int32_t blockAlign = 0;
    int32_t frameSize = 0;
    int32_t initialPadding = 0;
    int32_t seekPreRoll = 0;

    int32_t extraDataSize = 0;
    std::shared_ptr<uint8_t> extraData;

    AVRational timeBase = { 1, 1 };
};

struct VideoParam
{
    AVCodecID codecId = AV_CODEC_ID_NONE;
    uint32_t codecTag = 0;
    VideoFormat format{};

    int64_t bitRate = 0;
    int32_t bitsPerCodedSample = 0;
    int32_t bitsPerRawSample = 0;
    int32_t profile = 0;
    int32_t level = 0;

    AVFieldOrder fieldOrder = AV_FIELD_UNKNOWN;
    AVColorRange colorRange = AVCOL_RANGE_UNSPECIFIED;
    AVColorPrimaries colorPrimaries = AVCOL_PRI_RESERVED0;
    AVColorTransferCharacteristic colorTrc = AVCOL_TRC_RESERVED0;
    AVColorSpace colorSpace = AVCOL_SPC_RGB;
    AVChromaLocation chromaLocation = AVCHROMA_LOC_UNSPECIFIED;
    AVRational aspectRatio = {1, 1};
    bool hasBFrames = false;

    int32_t extraDataSize = 0;
    std::shared_ptr<uint8_t> extraData;

    AVRational timeBase = { 1, 1 };
    AVRational fps = { 0, 1 };
};

struct VideoCodecFormat
{
    AVCodecID codecId = AV_CODEC_ID_NONE;
    VideoFormat format;
};

struct CodecDeviceDesc
{
	u8string deviceIdentifier;
	u8string deviceDescription;
	uint32_t vendorId = 0;
	uint32_t subSysId = 0;
};

struct CodecDesc
{
	u8string id;
	CodecLevel codecLevel = CodecLevelNone;
	std::vector<AVPixelFormat> outputFormats;
};

class IPacketReader
{
public:
    virtual ~IPacketReader() = default;

    virtual FpState State() const = 0;

    virtual FpState Ready(int64_t timeoutMS) = 0;
    // FpStateOK FpStatePending
    virtual FpState PeekPacket(Packet & packet) = 0;
    // FpStateOK FpStateEOF FpStateTimeOut
    virtual FpState NextPacket(Packet & packet, int64_t timeoutMS) = 0;
};

class IAudioPacketReader : public IPacketReader
{
public:
    virtual ~IAudioPacketReader() = default;
    virtual AudioParam GetAudioDecodeParam() const = 0;
};

class IVideoPacketReader : public IPacketReader
{
public:
    virtual ~IVideoPacketReader() = default;
    virtual VideoParam GetVideoDecodeParam() const = 0;
};

class IAudioBufferInputStream
{
public:
    virtual ~IAudioBufferInputStream() = default;
    virtual FpState SetOutputFormat(AudioFormat format) = 0;

    virtual FpState Ready(int64_t timeoutMS) = 0;
    virtual FpState WaitForFrames(int64_t timeoutMS) = 0;
    // FpStateOK FpStatePending
    virtual FpState PeekBuffer(AudioBuffer & buffer) = 0;
    // FpStateOK FpStateEOF FpStateTimeOut
    virtual FpState NextBuffer(AudioBuffer & buffer, int64_t timeoutMS) = 0;
};

class IVideoBufferInputStream
{
public:
    virtual ~IVideoBufferInputStream() = default;
    virtual FpState SetOutputFormat(VideoFormat format) = 0;
    virtual VideoFormat OutputFormat() const = 0;

    virtual FpState Ready(int64_t timeoutMS) = 0;
    virtual FpState WaitForFrames(int64_t timeoutMS) = 0;
    // FpStateOK FpStatePending
    virtual FpState PeekBuffer(VideoBuffer & buffer) = 0;
    // FpStateOK FpStateEOF FpStateTimeOut
    virtual FpState NextBuffer(VideoBuffer & buffer, int64_t timeoutMS) = 0;

    virtual std::tuple<int64_t, int64_t> BufferQuality() const = 0;
};

class IVideoDecoderHWAccelContext
{
public:
    virtual ~IVideoDecoderHWAccelContext() = default;
    virtual FpState SetCodecFormat(VideoCodecFormat codecFormat) = 0;
    virtual FpState NeedReset() const = 0;
    virtual FpState Reset() = 0;
    virtual void * GetFFmpegHWAccelContext() = 0;
    virtual void * GetFFmpegHWDeviceContext() = 0;
    virtual AVPixelFormat GetOutputPixelFormat() const = 0;
    virtual FpState GetBuffer(std::shared_ptr<AVFrame> avframe, int32_t flags) = 0;
};

class IVideoDecoderHWAccelerator
{
public:
    virtual ~IVideoDecoderHWAccelerator() = default;
	virtual CodecDeviceDesc GetCodecDeviceDesc() const = 0;
	virtual std::map<AVCodecID, std::vector<CodecDesc>> GetCodecDescs() const = 0;
	virtual std::vector<CodecDesc> GetCodecDescs(AVCodecID codecId) const = 0;
	virtual std::vector<CodecDesc> GetCodecDescs(VideoCodecFormat codecFormat) const = 0;
    virtual std::tuple<AVHWDeviceType, std::vector<AVPixelFormat>> ChooseDevice(const std::vector<AVHWDeviceType> & hwDeviceTypes, VideoCodecFormat codecFormat) const = 0;
    virtual std::tuple<AVHWDeviceType, std::shared_ptr<IVideoDecoderHWAccelContext>> CreateAccelerator(const std::vector<AVHWDeviceType> & hwDeviceTypes, VideoCodecFormat codecFormat) = 0;
};

class IAudioDecoder : public IAudioBufferInputStream
{
public:
    virtual ~IAudioDecoder() = default;
    virtual std::shared_ptr<IAudioPacketReader> Stream() const = 0;
};

class IVideoDecoder : public IVideoBufferInputStream
{
public:
    virtual ~IVideoDecoder() = default;
    virtual std::shared_ptr<IVideoPacketReader> Stream() const = 0;
    virtual FpState SetVideoFormatCooperator(std::shared_ptr<IVideoDecoderHWAccelerator> formatCooperator) = 0;
    virtual VideoFormat DecodeFormat() const = 0;
};


class IAudioPlayer
{
public:
    virtual ~IAudioPlayer() = default;
    virtual FpState Start() = 0;
    virtual  std::tuple<FpState, std::shared_ptr<Clock>>  AddAudio(std::shared_ptr<IAudioBufferInputStream> audioDecoder) = 0;
};

class IVideoPlayer
{
public:
    virtual ~IVideoPlayer() = default;
};

class IVideoRenderWindow
{
public:
    virtual ~IVideoRenderWindow() = default;
    virtual void * GetHandle() const = 0;
};
