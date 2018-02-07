#pragma once

#include <map>
#include <memory>
#include <map>
#include <queue>
#include <functional>
#include <mutex>
#include <atomic>
#include "com_ptr.h"

const int TIME_BASE_S = AV_TIME_BASE;
const int TIME_BASE_MS = AV_TIME_BASE / 1000;

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

struct FpFrame
{
    std::shared_ptr<AVFrame> ptr;
    int64_t index = 0;
    int64_t pos = 0;
};

struct FpAudioFormat
{
    int32_t chanels;
    int32_t sampleRate;
    AVSampleFormat sampleFormat;
    int32_t bits;
    int32_t numBuffers;
    AVCodecID codecId;
    int32_t blockAlign;
    int32_t frameSize;
    int32_t padding;
};

class IFFmpegDemuxer
{
public:
    virtual ~IFFmpegDemuxer() = default;
    virtual FpError LoadFromFile(const u8string & filePath) = 0;
    virtual std::map<int, AVMediaType> GetStreamTypes() const = 0;
    virtual FpAudioFormat GetAudioFormat(int streamId) const = 0;
    virtual FpPacket NextPacket(int streamId) = 0;
};

class IAudioDecoderFP
{
public:
    virtual ~IAudioDecoderFP() = default;
    virtual std::shared_ptr<IFFmpegDemuxer> Demuxer() const = 0;
    virtual int32_t StreamIndex() const = 0;
    virtual FpAudioFormat GetOutputFormat() const = 0;
    virtual FpFrame NextFrame() = 0;
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
    virtual int doCombine(char * data, int width, int height, int strike, int & duration) = 0;
};

class IVideoRender
{
public:

};
