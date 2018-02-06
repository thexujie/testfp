#pragma once
#include "IMedia.h"
#include "avobject.h"
#include <memory>
#include <map>
#include <functional>

template<typename _Ty>
using MpVector = std::vector<_Ty>;

template<typename _Ty, size_t  _Size>
using MpArray = std::array<_Ty, _Size>;

template<class _Ty, void(Func)(_Ty * ptr)>
struct default_delete2
{	// default deleter for unique_ptr
    constexpr default_delete2() _NOEXCEPT = default;


    void operator()(_Ty * _Ptr) const _NOEXCEPT
    {	// delete a pointer
        static_assert(0 < sizeof(_Ty),
            "can't delete an incomplete type");
        Func(_Ptr);
    }
};


class AVFormatContextDeleter
{
public:
    AVFormatContextDeleter(AVFormatContext *& ptr){}
};

class MediaDemuxerFP : public IFFmpegDemuxer
{
public:
    MediaDemuxerFP();
    ~MediaDemuxerFP();

    MpError LoadFromFile(const u8string & filePath);

private:
    std::shared_ptr<AVFormatContext> m_avformatContext;
};

class MediaPlayerFP
{
public:
    MediaPlayerFP();
    ~MediaPlayerFP();

    MpError LoadFromFile(const u8string & filePath);

private:
    std::shared_ptr<AVFormatContext> m_avformatContext;
};
