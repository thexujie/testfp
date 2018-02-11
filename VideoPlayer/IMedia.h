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
    std::shared_ptr<AVFrame> avframe;
    std::shared_ptr<uint8_t> data;
    int64_t numSamples = 0;
    int64_t numSamplesRead = 0;
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

class IAudioDecoderFP;
class IAudioPacketReaderFP;

//class IFFmpegDemuxer
//{
//public:
//    virtual ~IFFmpegDemuxer() = default;
//    virtual FpState LoadFromFile(const u8string & filePath) = 0;
//    virtual std::shared_ptr<IAudioPacketReaderFP> GetAudioStream(int32_t streamId) = 0;
//
//    virtual std::map<int32_t, AVMediaType> GetStreamTypes() const = 0;
//    virtual FpAudioFormat GetAudioFormat(int32_t streamId) const = 0;
//    virtual FpState State(int32_t streamId) const = 0;
//
//    virtual FpState Ready(int64_t timeoutMS) = 0;
//    // FpStateOK FpStatePending
//    virtual FpState PeekPacket(int32_t streamId, FpPacket & packet) = 0;
//    // FpStateOK FpStateEOF FpStateTimeOut
//    virtual FpState NextPacket(int32_t streamId, FpPacket & packet, int64_t timeoutMS) = 0;
//};


class IAudioPacketReaderFP
{
public:
    virtual ~IAudioPacketReaderFP() = default;
    virtual FpAudioFormat GetAudioFormat() const = 0;
    virtual FpState State() const = 0;

    virtual FpState Ready(int64_t timeoutMS) = 0;
    // FpStateOK FpStatePending
    virtual FpState PeekPacket(FpPacket & packet) = 0;
    // FpStateOK FpStateEOF FpStateTimeOut
    virtual FpState NextPacket(FpPacket & packet, int64_t timeoutMS) = 0;
};

class IAudioBufferReaderFP
{
public:
    virtual ~IAudioBufferReaderFP() = default;

    virtual FpState SetOutputFormat(FpAudioFormat format) = 0;

    virtual FpState Ready(int64_t timeoutMS) = 0;
    virtual FpState WaitForFrames(int64_t timeoutMS) = 0;
    // FpStateOK FpStatePending
    virtual FpState PeekBuffer(FpAudioBuffer & buffer) = 0;
    // FpStateOK FpStateEOF FpStateTimeOut
    virtual FpState NextBuffer(FpAudioBuffer & buffer, int64_t timeoutMS) = 0;
};

class IAudioDecoderFP : public IAudioBufferReaderFP
{
public:
    virtual ~IAudioDecoderFP() = default;
    virtual std::shared_ptr<IAudioPacketReaderFP> Stream() const = 0;
    virtual FpState SetOutputFormat(FpAudioFormat format) = 0;

    virtual FpState Ready(int64_t timeoutMS) = 0;
    virtual FpState WaitForFrames(int64_t timeoutMS) = 0;
    // FpStateOK FpStatePending
    virtual FpState PeekBuffer(FpAudioBuffer & buffer) = 0;
    // FpStateOK FpStateEOF FpStateTimeOut
    virtual FpState NextBuffer(FpAudioBuffer & buffer, int64_t timeoutMS) = 0;
};

class IAudioPlayerFP
{
public:
    virtual ~IAudioPlayerFP() = default;
    virtual FpState Start() = 0;
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
