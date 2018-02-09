#pragma once
#include "IMedia.h"
#include "avobject.h"

template<typename _Ty>
using MpVector = std::vector<_Ty>;

template<typename _Ty, size_t  _Size>
using MpArray = std::array<_Ty, _Size>;

class MediaDemuxerFP : public IFFmpegDemuxer, public std::enable_shared_from_this<IFFmpegDemuxer>
{
public:
    MediaDemuxerFP();
    ~MediaDemuxerFP();

    FpState LoadFromFile(const u8string & filePath);
    std::shared_ptr<IAudioPacketStreamFP> GetAudioStream(int32_t streamId);

    FpState State(int32_t streamId) const;
    std::map<int32_t, AVMediaType> GetStreamTypes() const;
    FpAudioFormat GetAudioFormat(int32_t streamId) const;

    FpState Ready(int64_t timeoutMS);
    FpState PeekPacket(int32_t streamId, FpPacket & packet);
    FpState NextPacket(int32_t streamId, FpPacket & packet, int64_t timeoutMS);

private:
    void demuxerThread();

private:
    struct AvPacketQueue
    {
        std::queue<FpPacket> queue;
        int64_t streamIndex = 0;
        int64_t index = 0;
        std::atomic<FpState> state = FpStateOK;
        std::shared_ptr<IAudioPacketStreamFP> stream;
    };

    std::shared_ptr<AVFormatContext> _avformatContext;
    std::map<int32_t, AvPacketQueue> _packets;
    int64_t _packetIndex = 0;
    std::mutex _mtxRead;
    std::thread _thRead;
    std::condition_variable _condRead;
    std::condition_variable _condDemuxer;

    std::atomic<FpState> _state = FpStateOK;
    int32_t _maxPackets = 32;
    int32_t _minPackets = 16;
    int32_t _asyncTimeOutTime = 5000;
};

class AudioPacketStreamFP : public IAudioPacketStreamFP
{
public:
    AudioPacketStreamFP(std::shared_ptr<IFFmpegDemuxer> demuxer, int32_t streamIndex);
    ~AudioPacketStreamFP();

    FpAudioFormat GetAudioFormat() const;
    FpState State() const;

    FpState Ready(int64_t timeoutMS);
    // FpStateOK FpStatePending
    FpState PeekPacket(FpPacket & packet);
    // FpStateOK FpStateEOF FpStateTimeOut
    FpState NextPacket(FpPacket & packet, int64_t timeoutMS);

private:

    std::weak_ptr<IFFmpegDemuxer>  _demuxer;
    int32_t _streamIndex = -1;
};

class AudioDecoderFP : public IAudioDecoderFP
{
public:
    AudioDecoderFP(std::shared_ptr<IAudioPacketStreamFP> stream);
    ~AudioDecoderFP();

    std::shared_ptr<IAudioPacketStreamFP> Stream() const;
    FpAudioFormat GetOutputFormat() const;
    FpState SetOutputFormat(FpAudioFormat format);

    FpState Ready(int64_t timeoutMS);
    FpState WaitForFrames(int64_t timeoutMS);
    FpState PeekBuffer(FpAudioBuffer & buffer);
    FpState NextBuffer(FpAudioBuffer & buffer, int64_t timeoutMS);

private:
    FpState decodeFrame(std::shared_ptr<AVFrame> avframe, FpAudioBuffer & buffer);
    FpState readFrame(std::shared_ptr<AVFrame> & frame);
    void decoderThread();

private:
    std::shared_ptr<IAudioPacketStreamFP> _stream;

    FpAudioFormat _inputFormat;
    FpAudioFormat _outputFormat;

    std::shared_ptr<AVCodecContext> _codec;
    std::shared_ptr<SwrContext> _swr;

    std::list<FpAudioBuffer> _buffers;

    std::atomic<int64_t> _sessionIndex = 0;
    std::list<std::shared_ptr<AVFrame>> _sessionFrames;

    int64_t _bufferIndex = 0;
    std::mutex _mtxRead;
    std::thread _thread;
    std::condition_variable _condRead;
    std::condition_variable _condDecoder;
    std::atomic<FpState> _state = FpStateOK;

    int32_t _minFrames = 64;
    int32_t _maxFrames = 128;
    int32_t _asyncTimeOutTime = 5000;
};
