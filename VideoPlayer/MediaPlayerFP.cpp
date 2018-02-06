#include "stdafx.h"
#include "MediaPlayerFP.h"

void avformat_free_context_2(AVFormatContext *& ptr)
{
    avformat_free_context(ptr);
}

MediaDemuxerFP::MediaDemuxerFP()
{
    
}

MediaDemuxerFP::~MediaDemuxerFP()
{
}

MpError MediaDemuxerFP::LoadFromFile(const u8string & filePath)
{
    int averr = 0;

    AVFormatContext * avformatContext = nullptr;
    averr = avformat_open_input(&avformatContext, filePath.c_str(), NULL, NULL);
    if(averr || !avformatContext)
        return MpErrorGeneric;

    m_avformatContext.reset(avformatContext, avformat_free_context_2);

    //av_dump_format(m_avformatContext.get(), 0, filePath.c_str(), false);

    if(avformat_find_stream_info(m_avformatContext.get(), NULL) < 0)
        return MpErrorGeneric;

    return MpErrorOK;
}


MediaPlayerFP::MediaPlayerFP()
{
}


MediaPlayerFP::~MediaPlayerFP()
{
}


MpError MediaPlayerFP::LoadFromFile(const u8string & filePath)
{
    int averr = 0;
    avobject2<AVFormatContext, avformat_free_context> avformatContext(avformat_alloc_context());
    if(!avformatContext)
        return MpErrorNullptr;

    averr = avformat_open_input(&avformatContext, filePath.c_str(), NULL, NULL);
    if(averr)
        return MpErrorGeneric;

    //av_dump_format(avformatContext, 0, filePath.c_str(), false);

    if(avformat_find_stream_info(avformatContext, NULL) < 0)
        return MpErrorGeneric;

    return MpErrorOK;
}
