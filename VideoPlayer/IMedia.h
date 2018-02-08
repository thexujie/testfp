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

struct FpSource
{
    std::shared_ptr<AVFormatContext> avformatContext;
};

struct FpPacket
{
    std::shared_ptr<AVPacket> ptr;
    int64_t index;
    int64_t localIndex;
};

struct FpAudioBuffer
{
    int64_t index = 0;
    std::shared_ptr<uint8_t> data;
    int64_t numSamples = 0;
};

struct FpAudioFormat
{
    int32_t chanels = 0;
    int32_t sampleRate = 0;
    AVSampleFormat sampleFormat = AV_SAMPLE_FMT_NONE;
    int32_t bits = 0;
    int32_t numBufferedSamples = 0;
    AVCodecID codecId = AV_CODEC_ID_NONE;
};

class IFFmpegDemuxer
{
public:
    virtual ~IFFmpegDemuxer() = default;
    virtual FpError LoadFromFile(const u8string & filePath) = 0;
    virtual FpError State(int32_t streamId) const = 0;
    virtual std::map<int32_t, AVMediaType> GetStreamTypes() const = 0;
    virtual FpAudioFormat GetAudioFormat(int32_t streamId) const = 0;
    virtual FpPacket NextPacket(int32_t streamId) = 0;
};

class IAudioDecoderFP
{
public:
    virtual ~IAudioDecoderFP() = default;
    virtual std::shared_ptr<IFFmpegDemuxer> Demuxer() const = 0;
    virtual int32_t StreamIndex() const = 0;
    virtual FpAudioBuffer NextBuffer() = 0;
    virtual FpError ResetFormat(FpAudioFormat format) = 0;
};

class IAudioPlayerFP
{
public:
    virtual ~IAudioPlayerFP() = default;
    virtual FpError Start() = 0;
};

class IAudioDecoder
{
    
};

class IVideoPlayer
{
public:
    virtual ~IVideoPlayer() = default;
    virtual int32_t doCombine(char * data, int32_t width, int32_t height, int32_t strike, int32_t & duration) = 0;
};

class IVideoRender
{
public:

};
