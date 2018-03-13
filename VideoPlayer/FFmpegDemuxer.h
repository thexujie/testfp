#pragma once

#include "IMedia.h"

class FFmpegDemuxer : public std::enable_shared_from_this<FFmpegDemuxer>
{
public:
	FFmpegDemuxer();
	~FFmpegDemuxer();

	FpState LoadFromFile(const u8string & filePath);

    std::shared_ptr<AVFormatContext> GetAVFormatContext();
    std::shared_ptr<AVStream> GetAVStream(int32_t streamId);
    std::shared_ptr<AVCodecParameters> GetCodecParameters(int32_t streamId);
	std::shared_ptr<IAudioPacketReader> GetAudioStream(int32_t streamId);
	std::shared_ptr<IVideoPacketReader> GetVideoStream(int32_t streamId);

	FpState State(int32_t streamId) const;
	std::map<int32_t, AVMediaType> GetStreamTypes() const;
	AudioParam GetAudioFormat(int32_t streamId) const;

	FpState Ready(int64_t timeoutMS);
	FpState PeekPacket(int32_t streamId, Packet & packet);
	FpState NextPacket(int32_t streamId, Packet & packet, int64_t timeoutMS);

private:
	void demuxerThread();

private:
	struct AvPacketQueue
	{
		std::queue<Packet> queue;
		int64_t streamIndex = 0;
		int64_t index = 0;
		std::atomic<FpState> state = FpStateOK;
		std::weak_ptr<IPacketReader> stream;
	};

	std::shared_ptr<AVFormatContext> _avformatContext;
	std::map<int32_t, AvPacketQueue> _packets;
	int64_t _packetIndex = 0;
	std::mutex _mtx;
	std::thread _thDemuxer;
	std::condition_variable _condRead;
	std::condition_variable _condDemuxer;

	std::atomic<FpState> _state = FpStateOK;
    std::atomic<int32_t> _flags = FpFlagNone;
	int32_t _maxPackets = 32;
	int32_t _minPackets = 16;
	int32_t _asyncTimeOutTime = 5000;
};

class AudioPacketReaderFP : public IAudioPacketReader
{
public:
	AudioPacketReaderFP(std::shared_ptr<FFmpegDemuxer> demuxer, int32_t streamIndex);
	~AudioPacketReaderFP();

	std::shared_ptr<FFmpegDemuxer> Demuxer() const;
	int32_t StreamIndex() const;
	AudioParam GetAudioDecodeParam() const;
	FpState State() const;

	FpState Ready(int64_t timeoutMS);
	// FpStateOK FpStatePending
	FpState PeekPacket(Packet & packet);
	// FpStateOK FpStateEOF FpStateTimeOut
	FpState NextPacket(Packet & packet, int64_t timeoutMS);

private:
	std::shared_ptr<FFmpegDemuxer>  _demuxer;
	int32_t _streamIndex = -1;
    std::atomic<int32_t> _flags = FpFlagNone;
};

class VideoPacketReaderFP : public IVideoPacketReader
{
public:
	VideoPacketReaderFP(std::shared_ptr<FFmpegDemuxer> demuxer, int32_t streamIndex);
	~VideoPacketReaderFP();

	std::shared_ptr<FFmpegDemuxer> Demuxer() const;
	int32_t StreamIndex() const;
	VideoParam GetVideoDecodeParam() const;
	FpState State() const;

	FpState Ready(int64_t timeoutMS);
	// FpStateOK FpStatePending
	FpState PeekPacket(Packet & packet);
	// FpStateOK FpStateEOF FpStateTimeOut
	FpState NextPacket(Packet & packet, int64_t timeoutMS);

private:
	std::shared_ptr<FFmpegDemuxer>  _demuxer;
	int32_t _streamIndex = -1;
};
