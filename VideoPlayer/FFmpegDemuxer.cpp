#include "stdafx.h"
#include "FFmpegDemuxer.h"

FFmpegDemuxer::FFmpegDemuxer()
{

}

FFmpegDemuxer::~FFmpegDemuxer()
{
    if (_thDemuxer.joinable())
    {
        _flags = _flags | FpFlagStop;
        _condDemuxer.notify_all();
        _thDemuxer.join();
    }
}

FpState FFmpegDemuxer::LoadFromFile(const u8string & filePath)
{
	int32_t averr = 0;

	AVFormatContext * avformatContext = nullptr;
	averr = avformat_open_input(&avformatContext, filePath.c_str(), NULL, NULL);
	if(averr || !avformatContext)
		return FpStateGeneric;

	_avformatContext.reset(avformatContext, [](AVFormatContext * ptr) {avformat_free_context(ptr); });

	//av_dump_format(m_avformatContext.get(), 0, filePath.c_str(), false);

	if(avformat_find_stream_info(_avformatContext.get(), NULL) < 0)
		return FpStateGeneric;

	for(uint32_t ist = 0; ist < _avformatContext->nb_streams; ++ist)
		_packets[ist].streamIndex = ist;

	return FpStateOK;
}


std::shared_ptr<AVFormatContext> FFmpegDemuxer::GetAVFormatContext()
{
    return _avformatContext;
}

std::shared_ptr<AVStream> FFmpegDemuxer::GetAVStream(int32_t streamId)
{
    auto iter = _packets.find(streamId);
    if (iter == _packets.end())
        return {};
    return std::shared_ptr<AVStream>(_avformatContext->streams[streamId], [](void *) {});
}

std::shared_ptr<AVCodecParameters> FFmpegDemuxer::GetCodecParameters(int32_t streamId)
{
	auto iter = _packets.find(streamId);
	if(iter == _packets.end())
		return {};
	return std::shared_ptr<AVCodecParameters>(_avformatContext->streams[streamId]->codecpar, [](void *){});
}

std::shared_ptr<IAudioPacketReader> FFmpegDemuxer::GetAudioStream(int32_t streamId)
{
	auto iter = _packets.find(streamId);
	if(iter == _packets.end())
		return {};

	if(!iter->second.stream.expired())
		return std::dynamic_pointer_cast<IAudioPacketReader>(iter->second.stream.lock());

	std::shared_ptr<IAudioPacketReader> stream = std::make_shared<AudioPacketReaderFP>(shared_from_this(), streamId);
	iter->second.stream = stream;
	return stream;
}

std::shared_ptr<IVideoPacketReader> FFmpegDemuxer::GetVideoStream(int32_t streamId)
{
	auto iter = _packets.find(streamId);
	if(iter == _packets.end())
		return {};

	if(!iter->second.stream.expired())
		return std::dynamic_pointer_cast<IVideoPacketReader>(iter->second.stream.lock());

	std::shared_ptr<IVideoPacketReader> stream = std::make_shared<VideoPacketReaderFP>(shared_from_this(), streamId);
	iter->second.stream = stream;
	return stream;
}

FpState FFmpegDemuxer::State(int32_t streamId) const
{
	auto iter = _packets.find(streamId);
	if(iter == _packets.end())
		return FpStateNoData;

	return iter->second.state;
}

std::map<int32_t, AVMediaType> FFmpegDemuxer::GetStreamTypes() const
{
	std::map<int32_t, AVMediaType> types;
	if(_avformatContext)
	{
		for(uint32_t ist = 0; ist < _avformatContext->nb_streams; ++ist)
		{
			types[ist] = _avformatContext->streams[ist]->codecpar->codec_type;
		}
	}
	return types;
}

AudioDecodeParam FFmpegDemuxer::GetAudioFormat(int32_t streamId) const
{
	if(!_avformatContext || streamId < 0 || streamId >= static_cast<int32_t>(_avformatContext->nb_streams))
		return {};

	AVStream * stream = _avformatContext->streams[streamId];
	AVCodecParameters * avcodecAudioParameters = stream->codecpar;

	AudioDecodeParam param = {};
	param.codecId = avcodecAudioParameters->codec_id;
	param.codecTag = avcodecAudioParameters->codec_tag;

	param.format.chanels = avcodecAudioParameters->channels;
	param.format.sampleRate = avcodecAudioParameters->sample_rate;
	param.format.sampleFormat = (AVSampleFormat)avcodecAudioParameters->format;

	param.bitRate = avcodecAudioParameters->bit_rate;
	param.bitsPerCodedSample = avcodecAudioParameters->bits_per_coded_sample;
	param.bitsPerRawSample = avcodecAudioParameters->bits_per_raw_sample;
	param.profile = avcodecAudioParameters->profile;
	param.level = avcodecAudioParameters->level;

	param.blockAlign = avcodecAudioParameters->block_align;
	param.frameSize = avcodecAudioParameters->frame_size;
	param.initialPadding = avcodecAudioParameters->initial_padding;
	param.seekPreRoll = avcodecAudioParameters->seek_preroll;

	if(avcodecAudioParameters->extradata && avcodecAudioParameters->extradata_size > 0)
	{
		param.extraData = std::shared_ptr<uint8_t>((uint8_t *)av_malloc(avcodecAudioParameters->extradata_size), av_free);
		memcpy(param.extraData.get(), avcodecAudioParameters->extradata, avcodecAudioParameters->extradata_size);
		param.extraDataSize = avcodecAudioParameters->extradata_size;
	}
	return param;
}

FpState FFmpegDemuxer::Ready(int64_t timeoutMS)
{
	if(!timeoutMS)
		timeoutMS = _asyncTimeOutTime;

	if(_thDemuxer.get_id() != std::thread::id())
		return FpStateAlready;

	std::unique_lock<std::mutex> lock(_mtx);
	_thDemuxer = std::thread(std::bind(&FFmpegDemuxer::demuxerThread, this));
	if(_condRead.wait_for(lock, std::chrono::milliseconds(_asyncTimeOutTime)) == std::cv_status::timeout)
		return FpStateTimeOut;
	return FpStateOK;
}

FpState FFmpegDemuxer::PeekPacket(int32_t streamId, Packet & packet)
{
	if(_thDemuxer.get_id() == std::thread::id())
	{
		_thDemuxer = std::thread(std::bind(&FFmpegDemuxer::demuxerThread, this));
		return FpStatePending;
	}

	if(_packets.find(streamId) == _packets.end())
		return FpStateNoData;

	if(_state < 0 && _state != FpStateEOF)
		return _state;

	std::unique_lock<std::mutex> lock(_mtx);
	if(_packets[streamId].queue.empty())
	{
		if(_state == FpStateEOF)
			return FpStateEOF;
		return FpStatePending;
	}

	packet = _packets[streamId].queue.front();
	_packets[streamId].queue.pop();

	lock.unlock();
	_condDemuxer.notify_one();
	return FpStateOK;
}

FpState FFmpegDemuxer::NextPacket(int32_t streamId, Packet & packet, int64_t timeoutMS)
{
	if(!timeoutMS)
		timeoutMS = _asyncTimeOutTime;

	if(_state < 0 && _state != FpStateEOF)
		return _state;

	std::unique_lock<std::mutex> lock(_mtx);
	if(_packets.find(streamId) == _packets.end())
		return FpStateNoData;

	if(_thDemuxer.get_id() == std::thread::id())
	{
		_thDemuxer = std::thread(std::bind(&FFmpegDemuxer::demuxerThread, this));
		auto wait = _condRead.wait_for(lock, std::chrono::milliseconds(timeoutMS));
		if(wait == std::cv_status::timeout)
			return FpStateTimeOut;
	}

	if(!_packets[streamId].queue.empty())
	{
		packet = _packets[streamId].queue.front();
		_packets[streamId].queue.pop();

		_condDemuxer.notify_one();
		return FpStateOK;
	}

	if(_state == FpStateEOF)
		return FpStateEOF;

	_condDemuxer.notify_one();
	auto wait = _condRead.wait_for(lock,
		std::chrono::milliseconds(_asyncTimeOutTime),
		[this, streamId] {return !_packets[streamId].queue.empty() || _state < 0; });

	if(!wait)
		return FpStateTimeOut;

	if(_state < 0)
		return _state;

	if(_packets[streamId].queue.empty())
		return FpStateEOF;

	packet = _packets[streamId].queue.front();
	_packets[streamId].queue.pop();

	_condDemuxer.notify_one();
	return FpStateOK;
}

void FFmpegDemuxer::demuxerThread()
{
	thread_set_name(0, "demuxerThread");

	if(!_avformatContext)
	{
		_state = FpStateBadState;
		return;
	}

	//std::this_thread::sleep_for(std::chrono::seconds(2));
	int32_t averr = 0;
	while(!_state)
	{
		std::unique_lock<std::mutex> lock(_mtx);
		_condDemuxer.wait(lock, [this]
		{
            if (_flags & FpFlagStop)
                return true;

			for(auto iter = _packets.begin(); iter != _packets.end(); ++iter)
			{
				if(!iter->second.stream.expired() && iter->second.queue.size() < _minPackets)
					return true;
			}
			return false;
		}
		);

        if (_flags & FpFlagStop)
            break;

		for(auto iter = _packets.begin(); iter != _packets.end(); ++iter)
		{
			auto & packets = iter->second;
			while(!packets.stream.expired() && packets.queue.size() < _maxPackets && _state >= 0)
			{
				std::shared_ptr<AVPacket> avpacket(av_packet_alloc(), av_packet_unref);
				av_init_packet(avpacket.get());
				averr = av_read_frame(_avformatContext.get(), avpacket.get());
				if(averr == AVERROR_EOF)
				{
					//ц╩ак
					_state = FpStateEOF;;
					break;
				}

				if(averr)
				{
					_state = FpStateGeneric;;
					log("av_read_frame %d.\n", averr);
				}
				else
				{
					int32_t streamId = avpacket->stream_index;
					if(!_packets[streamId].stream.expired())
					{
						_packets[streamId].queue.push({ avpacket, _packetIndex++, _packets[streamId].index });
						++_packets[streamId].index;
					}
				}
			}
		}

		lock.unlock();
		_condRead.notify_all();
	}
}

AudioPacketReaderFP::AudioPacketReaderFP(std::shared_ptr<FFmpegDemuxer> demuxer, int32_t streamIndex)
	:_demuxer(demuxer), _streamIndex(streamIndex)
{

}

AudioPacketReaderFP::~AudioPacketReaderFP()
{
}

std::shared_ptr<FFmpegDemuxer> AudioPacketReaderFP::Demuxer() const
{
	return _demuxer;
}

int32_t AudioPacketReaderFP::StreamIndex() const
{
	return _streamIndex;
}

AudioDecodeParam AudioPacketReaderFP::GetAudioDecodeParam() const
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	return _demuxer->GetAudioFormat(_streamIndex);
}

FpState AudioPacketReaderFP::State() const
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	return _demuxer->State(_streamIndex);
}

FpState AudioPacketReaderFP::Ready(int64_t timeoutMS)
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	return _demuxer->Ready(timeoutMS);
}

// FpStateOK FpStatePending
FpState AudioPacketReaderFP::PeekPacket(Packet & packet)
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	return _demuxer->PeekPacket(_streamIndex, packet);
}

// FpStateOK FpStateEOF FpStateTimeOut
FpState AudioPacketReaderFP::NextPacket(Packet & packet, int64_t timeoutMS)
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	return _demuxer->NextPacket(_streamIndex, packet, timeoutMS);
}



VideoPacketReaderFP::VideoPacketReaderFP(std::shared_ptr<FFmpegDemuxer> demuxer, int32_t streamIndex)
	:_demuxer(demuxer), _streamIndex(streamIndex)
{

}

VideoPacketReaderFP::~VideoPacketReaderFP()
{

}

std::shared_ptr<FFmpegDemuxer> VideoPacketReaderFP::Demuxer() const
{
	return _demuxer;
}

int32_t VideoPacketReaderFP::StreamIndex() const
{
	return _streamIndex;
}

VideoDecodeParam VideoPacketReaderFP::GetVideoDecodeParam() const
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	std::shared_ptr<AVStream> stream = _demuxer->GetAVStream(_streamIndex);
	if(!stream)
		return {};

	VideoDecodeParam decodeParam = {};
	decodeParam.codecId = stream->codecpar->codec_id;
	decodeParam.codecTag = stream->codecpar->codec_tag;

	decodeParam.format.width = stream->codecpar->width;
	decodeParam.format.height = stream->codecpar->height;
	decodeParam.format.pixelFormat = (AVPixelFormat)stream->codecpar->format;

	decodeParam.bitRate = stream->codecpar->bit_rate;
	decodeParam.bitsPerCodedSample = stream->codecpar->bits_per_coded_sample;
	decodeParam.bitsPerRawSample = stream->codecpar->bits_per_raw_sample;
	decodeParam.profile = stream->codecpar->profile;
	decodeParam.level = stream->codecpar->level;

	decodeParam.fieldOrder= stream->codecpar->field_order;
	decodeParam.colorRange = stream->codecpar->color_range;
	decodeParam.colorPrimaries = stream->codecpar->color_primaries;
	decodeParam.colorTrc = stream->codecpar->color_trc;
	decodeParam.colorSpace = stream->codecpar->color_space;
	decodeParam.chromaLocation = stream->codecpar->chroma_location;
	decodeParam.aspectRatio = stream->codecpar->sample_aspect_ratio;
	decodeParam.hasBFrames = stream->codecpar->video_delay;

    decodeParam.timeBase = stream->time_base;
    decodeParam.fps = av_guess_frame_rate(_demuxer->GetAVFormatContext().get(), stream.get(), nullptr);

	if(stream->codecpar->extradata && stream->codecpar->extradata_size > 0)
	{
		decodeParam.extraData = std::shared_ptr<uint8_t>((uint8_t *)av_malloc(stream->codecpar->extradata_size), av_free);
		memcpy(decodeParam.extraData.get(), stream->codecpar->extradata, stream->codecpar->extradata_size);
		decodeParam.extraDataSize = stream->codecpar->extradata_size;
	}
	return decodeParam;
}

FpState VideoPacketReaderFP::State() const
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	return _demuxer->State(_streamIndex);
}

FpState VideoPacketReaderFP::Ready(int64_t timeoutMS)
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	return _demuxer->Ready(timeoutMS);
}

// FpStateOK FpStatePending
FpState VideoPacketReaderFP::PeekPacket(Packet & packet)
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	return _demuxer->PeekPacket(_streamIndex, packet);
}

// FpStateOK FpStateEOF FpStateTimeOut
FpState VideoPacketReaderFP::NextPacket(Packet & packet, int64_t timeoutMS)
{
	if(!_demuxer || _streamIndex < 0)
		throw std::exception();

	return _demuxer->NextPacket(_streamIndex, packet, timeoutMS);
}
