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
    FpError State(int32_t streamId) const;
    std::map<int32_t, AVMediaType> GetStreamTypes() const;
    FpAudioFormat GetAudioFormat(int32_t streamId) const;
    FpPacket NextPacket(int32_t streamId);

private:
    void demuxerThread();

private:
    struct AvPacketQueue
    {
        std::queue<FpPacket> queue;
        int64_t index = 0;
        std::atomic<FpError> state = FpErrorOK;
    };
    std::shared_ptr<AVFormatContext> _avformatContext;
    std::map<int32_t, AvPacketQueue> _packets;
    int64_t _packetIndex = 0;
    std::mutex _mtxRead;
    std::thread _thRead;
    std::condition_variable _condRead;
    std::condition_variable _condDemuxer;

    std::atomic<FpError> _state = FpErrorOK;
    int32_t _maxPackets = 32;
    int32_t _minPackets = 16;
};

class AudioDecoderFP : public IAudioDecoderFP
{
public:
    AudioDecoderFP(std::shared_ptr<IFFmpegDemuxer> demuxer, int32_t streamIndex);
    ~AudioDecoderFP();

    std::shared_ptr<IFFmpegDemuxer> Demuxer() const;
    int32_t StreamIndex() const;
    FpAudioFormat GetOutputFormat() const;

    FpAudioBuffer NextBuffer();
    FpError ResetFormat(FpAudioFormat format);

private:
    void decoderThread();

private:
    std::shared_ptr<IFFmpegDemuxer> _demuxer;
    int32_t _streamIndex;

    FpAudioFormat _inputFormat;
    FpAudioFormat _outputFormat;

    std::shared_ptr<AVCodecContext> _codec;
    std::shared_ptr<SwrContext> _swr;

    std::list<FpAudioBuffer> _buffers;

    int64_t _bufferIndex = 0;
    std::mutex _mtxRead;
    std::thread _thread;
    std::condition_variable _condRead;
    std::condition_variable _condDecoder;
    std::atomic<FpError> _state = FpErrorOK;

    int32_t _minFrames = 64;
    int32_t _maxFrames = 128;
};
