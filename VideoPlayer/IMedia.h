#pragma once

const int TIME_BASE_S = AV_TIME_BASE;
const int TIME_BASE_MS = AV_TIME_BASE / 1000;

typedef std::string a8string;
typedef std::string u8string;

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

enum MpError
{
    MpErrorOK = 0,
    MpErrorGeneric,
    MpErrorNullptr,
    MpErrorInvalidParams,
};

class IFFmpegDemuxer
{
public:
    virtual ~IFFmpegDemuxer() = default;
    virtual std::shared_ptr<AVPacket> NextPacket() = 0;
};

class IAudioDecoder
{
    
};

class IVideoPlayer
{
public:
    virtual ~IVideoPlayer() = default;
    virtual int doCombine(byte * data, int width, int height, int strike, int & duration) = 0;
};

class IVideoRender
{
public:

};
