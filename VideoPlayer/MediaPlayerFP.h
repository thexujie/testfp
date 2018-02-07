#pragma once
#include "IMedia.h"
#include "avobject.h"

template<typename _Ty>
using MpVector = std::vector<_Ty>;

template<typename _Ty, size_t  _Size>
using MpArray = std::array<_Ty, _Size>;

class MediaDemuxerFP : public IFFmpegDemuxer
{
public:
    MediaDemuxerFP();
    ~MediaDemuxerFP();

    FpError LoadFromFile(const u8string & filePath);
    std::map<int, AVMediaType> GetStreamTypes() const;
    FpAudioFormat GetAudioFormat(int streamId) const;
    FpPacket NextPacket(int streamId);

private:
    void demuxerThread();

private:
    struct AvPacketQueue
    {
        std::queue<FpPacket> queue;
        int64_t index;
    };
    std::shared_ptr<AVFormatContext> _avformatContext;
    std::map<int, AvPacketQueue> _packets;
    int64_t _packetIndex = 0;
    std::mutex _mtxRead;
    std::thread _thRead;
    std::condition_variable _condRead;
    std::condition_variable _condDemuxer;

    std::atomic<bool> _readEnd = false;
    int32_t _numMaxPackets = 16;
    int32_t _numMinPackets = 8;
};

class AudioDecoderFP : public IAudioDecoderFP
{
public:
    AudioDecoderFP(std::shared_ptr<IFFmpegDemuxer> demuxer, int32_t streamIndex);
    ~AudioDecoderFP();

    std::shared_ptr<IFFmpegDemuxer> Demuxer() const;
    int32_t StreamIndex() const;
    FpAudioFormat GetOutputFormat() const;

    FpFrame NextFrame();
    FpError ResetFormat(FpAudioFormat format);

private:
    void decoderThread();

private:
    std::shared_ptr<IFFmpegDemuxer> _demuxer;
    int32_t _streamIndex;

    FpAudioFormat _inputFormat;
    FpAudioFormat _outputFormat;

    std::queue<FpFrame> _frames;

    int64_t _frameIndex = 0;
    std::mutex _mtxRead;
    std::thread _thread;
    std::condition_variable _condRead;
    std::condition_variable _condDemuxer;
    std::atomic<bool> _readEnd = false;
    int32_t _minFrames = 8;
    int32_t _maxFrames = 16;
};
