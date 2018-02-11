#include "stdafx.h"
#include "MediaPlayerFP.h"

#define DEMUXER_ASYNCHRONOUS 1
#define DECODE_ASYNCHRONOUS 1

void avformat_free_context_2(AVFormatContext *& ptr)
{
    avformat_free_context(ptr);
}

void avcodec_free_context_wrap(AVCodecContext * ptr)
{
    avcodec_free_context(&ptr);
}

void av_packet_free_wap(AVPacket * ptr)
{
    av_packet_free(&ptr);
}

void av_frame_free_wrap(AVFrame * ptr)
{
    av_frame_free(&ptr);
}

void swr_free_wrap(SwrContext * ptr)
{
    swr_free(&ptr);
}

FFmpegDemuxerFP::FFmpegDemuxerFP()
{

}

FFmpegDemuxerFP::~FFmpegDemuxerFP()
{
    _thRead.join();
}

FpState FFmpegDemuxerFP::LoadFromFile(const u8string & filePath)
{
    int32_t averr = 0;

    AVFormatContext * avformatContext = nullptr;
    averr = avformat_open_input(&avformatContext, filePath.c_str(), NULL, NULL);
    if (averr || !avformatContext)
        return FpStateGeneric;

    _avformatContext.reset(avformatContext, avformat_free_context_2);

    //av_dump_format(m_avformatContext.get(), 0, filePath.c_str(), false);

    if (avformat_find_stream_info(_avformatContext.get(), NULL) < 0)
        return FpStateGeneric;

    for (int32_t ist = 0; ist < _avformatContext->nb_streams; ++ist)
        _packets[ist].streamIndex = ist;

    return FpStateOK;
}

std::shared_ptr<IAudioPacketReaderFP> FFmpegDemuxerFP::GetAudioStream(int32_t streamId)
{
    auto iter = _packets.find(streamId);
    if (iter == _packets.end())
        return {};

    if (iter->second.stream)
        return iter->second.stream;

    iter->second.stream = std::make_shared<AudioPacketReaderFP>(shared_from_this(), streamId);
    return iter->second.stream;
}

FpState FFmpegDemuxerFP::State(int32_t streamId) const
{
    auto iter = _packets.find(streamId);
    if (iter == _packets.end())
        return FpStateNoData;

    return iter->second.state;
}

std::map<int32_t, AVMediaType> FFmpegDemuxerFP::GetStreamTypes() const
{
    std::map<int32_t, AVMediaType> types;
    if (_avformatContext)
    {
        for (int32_t ist = 0; ist < _avformatContext->nb_streams; ++ist)
        {
            types[ist] = _avformatContext->streams[ist]->codecpar->codec_type;
        }
    }
    return types;
}

FpAudioFormat FFmpegDemuxerFP::GetAudioFormat(int32_t streamId) const
{
    FpAudioFormat format = {};
    if (!_avformatContext || streamId < 0 || streamId >= _avformatContext->nb_streams)
        return format;

    AVStream * stream = _avformatContext->streams[streamId];
    AVCodecParameters * avcodecAudioParameters = stream->codecpar;

    format.chanels = avcodecAudioParameters->channels;
    format.sampleRate = avcodecAudioParameters->sample_rate;
    format.sampleFormat = (AVSampleFormat)avcodecAudioParameters->format;
    format.bits = avcodecAudioParameters->bits_per_coded_sample;
    format.numBufferedSamples = 0;
    format.codecId = avcodecAudioParameters->codec_id;
    return format;
}

FpState FFmpegDemuxerFP::Ready(int64_t timeoutMS)
{
    if (!timeoutMS)
        timeoutMS = _asyncTimeOutTime;

    if (_thRead.get_id() != std::thread::id())
        return FpStateAlready;

    std::unique_lock<std::mutex> lock(_mtxRead);
    _thRead = std::thread(std::bind(&FFmpegDemuxerFP::demuxerThread, this));
    if (_condRead.wait_for(lock, std::chrono::milliseconds(_asyncTimeOutTime)) == std::cv_status::timeout)
        return FpStateTimeOut;
    return FpStateOK;
}

FpState FFmpegDemuxerFP::PeekPacket(int32_t streamId, FpPacket & packet)
{
    if (_thRead.get_id() == std::thread::id())
    {
        _thRead = std::thread(std::bind(&FFmpegDemuxerFP::demuxerThread, this));
        return FpStatePending;
    }

    if (_packets.find(streamId) == _packets.end())
        return FpStateNoData;

    if (_state < 0)
        return _state;

    std::unique_lock<std::mutex> lock(_mtxRead);
    if (_packets[streamId].queue.empty())
        return FpStatePending;

    packet = _packets[streamId].queue.front();
    _packets[streamId].queue.pop();

    lock.unlock();
    _condDemuxer.notify_one();
    return FpStateOK;
}

FpState FFmpegDemuxerFP::NextPacket(int32_t streamId, FpPacket & packet, int64_t timeoutMS)
{
    if (!timeoutMS)
        timeoutMS = _asyncTimeOutTime;

    std::unique_lock<std::mutex> lock(_mtxRead);
    if (_packets.find(streamId) == _packets.end())
        return FpStateEOF;

    if (_thRead.get_id() == std::thread::id())
    {
        _thRead = std::thread(std::bind(&FFmpegDemuxerFP::demuxerThread, this));
        auto wait = _condRead.wait_for(lock, std::chrono::milliseconds(timeoutMS));
        if (wait == std::cv_status::timeout)
            return FpStateTimeOut;
    }

    if (!_packets[streamId].queue.empty())
    {
        packet = _packets[streamId].queue.front();
        _packets[streamId].queue.pop();

        _condDemuxer.notify_one();
        return FpStateOK;
    }

    _condDemuxer.notify_one();
    auto wait = _condRead.wait_for(lock,
        std::chrono::milliseconds(_asyncTimeOutTime),
        [this, streamId] {return !_packets[streamId].queue.empty() || _state < 0; });
        
    if (!wait)
        return FpStateTimeOut;

    if (_state < 0)
        return _state;

    if (_packets[streamId].queue.empty())
        return FpStateEOF;

    packet = _packets[streamId].queue.front();
    _packets[streamId].queue.pop();

    _condDemuxer.notify_one();
    return FpStateOK;
}

void FFmpegDemuxerFP::demuxerThread()
{
    thread_set_name(0, "demuxerThread");

    if (!_avformatContext)
    {
        _state = FpStateBadState;
        return;
    }
    
    //std::this_thread::sleep_for(std::chrono::seconds(2));
    int32_t averr = 0;
    while (!_state)
    {
        std::unique_lock<std::mutex> lock(_mtxRead);
        _condDemuxer.wait(lock,[this]
        {
            for (auto iter = _packets.begin(); iter != _packets.end(); ++iter)
            {
                if (iter->second.stream && iter->second.queue.size() < _minPackets)
                    return true;
            }
            return false;
        }
            );

        for (auto iter = _packets.begin(); iter != _packets.end(); ++iter)
        {
            auto & packets = iter->second;
            while (packets.stream && packets.queue.size() < _maxPackets && _state >= 0)
            {
                std::shared_ptr<AVPacket> avpacket(av_packet_alloc(), av_packet_unref);
                av_init_packet(avpacket.get());
                averr = av_read_frame(_avformatContext.get(), avpacket.get());
                if (averr == AVERROR_EOF)
                {
                    //没了
                    _state = FpStateEOF;;
                    break;
                }

                if (averr)
                {
                    _state = FpStateGeneric;;
                    log("av_read_frame %d.\n", averr);
                }
                else
                {
                    int32_t streamId = avpacket->stream_index;
                    if(_packets[streamId].stream)
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


AudioPacketReaderFP::AudioPacketReaderFP(std::shared_ptr<FFmpegDemuxerFP> demuxer, int32_t streamIndex)
    :_demuxer(demuxer), _streamIndex(streamIndex)
{
    
}

AudioPacketReaderFP::~AudioPacketReaderFP()
{
    
}

FpAudioFormat AudioPacketReaderFP::GetAudioFormat() const
{
    if (_demuxer.expired() || _streamIndex < 0)
        throw std::exception();

    return _demuxer.lock()->GetAudioFormat(_streamIndex);
}

FpState AudioPacketReaderFP::State() const
{
    if (_demuxer.expired() || _streamIndex < 0)
        throw std::exception();

    return _demuxer.lock()->State(_streamIndex);
}

FpState AudioPacketReaderFP::Ready(int64_t timeoutMS)
{
    if (_demuxer.expired() || _streamIndex < 0)
        throw std::exception();

    return _demuxer.lock()->Ready(timeoutMS);
}

// FpStateOK FpStatePending
FpState AudioPacketReaderFP::PeekPacket(FpPacket & packet)
{
    if (_demuxer.expired() || _streamIndex < 0)
        throw std::exception();

    return _demuxer.lock()->PeekPacket(_streamIndex, packet);
}

// FpStateOK FpStateEOF FpStateTimeOut
FpState AudioPacketReaderFP::NextPacket(FpPacket & packet, int64_t timeoutMS)
{
    if (_demuxer.expired() || _streamIndex < 0)
        throw std::exception();

    return _demuxer.lock()->NextPacket(_streamIndex, packet, timeoutMS);
}

AudioDecoderFP::AudioDecoderFP(std::shared_ptr<IAudioPacketReaderFP> reader)
    : _reader(reader)
{
    _inputFormat = _reader->GetAudioFormat();
}

AudioDecoderFP::~AudioDecoderFP()
{
    
}

std::shared_ptr<IAudioPacketReaderFP> AudioDecoderFP::Stream() const
{
    return _reader;
}

FpAudioFormat AudioDecoderFP::GetOutputFormat() const
{
    if (!_reader)
        return {};

    return _reader->GetAudioFormat();
}

FpState AudioDecoderFP::SetOutputFormat(FpAudioFormat format)
{
    std::unique_lock<std::mutex> lock(_mtxRead);
    _outputFormat = format;
    ++_sessionIndex;
    for (auto iter = _buffers.begin(); iter != _buffers.end(); ++iter)
        _sessionFrames.push_front(iter->avframe);
    _buffers.clear();
    _condDecoder.notify_all();
    return FpStateOK;
}

FpState AudioDecoderFP::Ready(int64_t timeoutMS)
{
    if (!timeoutMS)
        timeoutMS = _asyncTimeOutTime;

    if (_thread.get_id() != std::thread::id())
        return FpStateAlready;

    std::unique_lock<std::mutex> lock(_mtxRead);
    _thread = std::thread(std::bind(&AudioDecoderFP::decoderThread, this));
    if (_condRead.wait_for(lock, std::chrono::milliseconds(timeoutMS)) == std::cv_status::timeout)
        return FpStateTimeOut;
    else
        return FpStateOK;
}

FpState AudioDecoderFP::WaitForFrames(int64_t timeoutMS)
{
    if (!timeoutMS)
        timeoutMS = _asyncTimeOutTime;

    if (_thread.get_id() == std::thread::id())
        _thread = std::thread(std::bind(&AudioDecoderFP::decoderThread, this));

    std::unique_lock<std::mutex> lock(_mtxRead);
    if (_condRead.wait_for(lock, std::chrono::milliseconds(timeoutMS), [this] {return !_buffers.empty(); }))
        return FpStateOK;
    else
        return FpStateTimeOut;
}

FpState AudioDecoderFP::PeekBuffer(FpAudioBuffer & buffer)
{
    if (_outputFormat.sampleFormat == AV_SAMPLE_FMT_NONE)
        return {};

    if (_thread.get_id() == std::thread::id())
    {
        _thread = std::thread(std::bind(&AudioDecoderFP::decoderThread, this));
        return FpStatePending;
    }

    std::unique_lock<std::mutex> lock(_mtxRead);
    if (_buffers.empty())
    {
        //badly...
        lock.unlock();
        _condDecoder.notify_one();
        return FpStatePending;
    }

    buffer = _buffers.front();
    _buffers.pop_front();
    lock.unlock();
    _condDecoder.notify_one();
    return FpStateOK;
}

FpState AudioDecoderFP::NextBuffer(FpAudioBuffer & buffer, int64_t timeoutMS)
{
    if (!timeoutMS)
        timeoutMS = _asyncTimeOutTime;

    if (_outputFormat.sampleFormat == AV_SAMPLE_FMT_NONE)
        return FpStateEOF;

    std::unique_lock<std::mutex> lock(_mtxRead);
    if (_thread.get_id() == std::thread::id())
    {
        _thread = std::thread(std::bind(&AudioDecoderFP::decoderThread, this));
        if (_condRead.wait_for(lock, std::chrono::milliseconds(timeoutMS)) == std::cv_status::timeout)
            return FpStateTimeOut;
    }

    if (!_buffers.empty())
    {
        buffer = _buffers.front();
        _buffers.pop_front();
        _condDecoder.notify_one();
        return FpStateOK;
    }

    //badly...
    _condDecoder.notify_one();
    if (!_condRead.wait_for(lock, std::chrono::milliseconds(timeoutMS), [this] { return !_buffers.empty() || _state < 0; }))
        return FpStateTimeOut;

    if (_state < 0)
        return _state;

    if (_buffers.empty())
        return FpStateEOF;

    buffer = _buffers.front();
    _buffers.pop_front();
    _condDecoder.notify_one();

    return FpStateOK;
}

FpState AudioDecoderFP::decodeFrame(std::shared_ptr<AVFrame> avframe, FpAudioBuffer & buffer)
{
    int32_t averr = 0;
    uint32_t bytesPerSample = av_get_bytes_per_sample(_outputFormat.sampleFormat) * _outputFormat.chanels;
    buffer.avframe = avframe;
    buffer.numSamples = swr_get_out_samples(_swr.get(), avframe->nb_samples);
    buffer.data = std::shared_ptr<uint8_t>(static_cast<uint8_t *>(av_malloc(buffer.numSamples * bytesPerSample)), av_free);

    int64_t numSamples = 0;
    while (numSamples < buffer.numSamples)
    {
        uint8_t * dst = buffer.data.get() + numSamples * bytesPerSample;
        if (!numSamples)
            averr = swr_convert(_swr.get(), &dst, buffer.numSamples - numSamples, (const uint8_t **)avframe->data, avframe->nb_samples);
        else
            averr = swr_convert(_swr.get(), &dst, buffer.numSamples - numSamples, 0, 0);

        if (averr == 0)
            break;

        if (averr < 0)
        {
            _state = FpStateBadState;
            break;
        }

        numSamples += averr;
    }

    buffer.numSamples = numSamples;
    return FpStateOK;
}

FpState AudioDecoderFP::readFrame(std::shared_ptr<AVFrame> & frame)
{
    if(!_sessionFrames.empty())
    {
        frame = _sessionFrames.back();
        _sessionFrames.pop_back();
        return FpStateOK;
    }

    int32_t averr = 0;
    frame.reset(av_frame_alloc(), av_frame_free_wrap);
    // receive a frame
    FpState state = FpStateOK;
    while (state >= 0)
    {
        averr = avcodec_receive_frame(_codec.get(), frame.get());
        if (!averr)
            break;

        if (averr == AVERROR(EAGAIN))
        {
            FpPacket packet{};
            state = _reader->NextPacket(packet, std::numeric_limits<uint32_t>::max());

            if (state < 0)
                break;

            if(!packet.ptr)
            {
                state = FpStateEOF;
                break;
            }

            //printf("packet [%lld] pos=%lld, pts=%lld, dur=%lld.\n", packet.localIndex, packet.ptr->pos, packet.ptr->pts, packet.ptr->duration);

            averr = avcodec_send_packet(_codec.get(), packet.ptr.get());
            if (averr < 0)
            {
                state = FpStateBadState;
                break;
            }
        }
        else
        {
            state = FpStateBadState;
            break;
        }
    }
    return state;
}

void AudioDecoderFP::decoderThread()
{
    thread_set_name(0, "decoderThread");
    //thread_prom();

    if (!_reader || !_inputFormat.codecId || _outputFormat.sampleFormat == AV_SAMPLE_FMT_NONE)
    {
        _state = FpStateBadState;
        return;
    }

    int32_t averr = 0;
    if (!_codec)
    {
        AVCodec * avcodec = avcodec_find_decoder(_inputFormat.codecId);
        if (!avcodec)
        {
            _state = FpStateBadState;
            return;
        }
        _codec.reset(avcodec_alloc_context3(avcodec), avcodec_free_context_wrap);
        _codec->sample_fmt = _inputFormat.sampleFormat;
        _codec->channels = _inputFormat.chanels;
        //_codec->channel_layout = av_get_default_channel_layout(_inputFormat.chanels);;
        _codec->sample_rate = _inputFormat.sampleRate;

        if (avcodec_open2(_codec.get(), avcodec, NULL) < 0)
        {
            _state = FpStateBadState;
            return;
        }
    }

    //-------------------------------------------------------------
    int64_t sessionIndex = _sessionIndex - 1;
    std::list<FpAudioBuffer> buffers;
    FpState error = FpStateOK;
    while (_state >= 0 && error >= 0)
    {
        while (_state >= 0 && error >= 0 && buffers.size() < _minFrames && sessionIndex == _sessionIndex)
        {
            std::shared_ptr<AVFrame> avframe;
            _state = readFrame(avframe);
            if (_state != 0)
                break;

            FpAudioBuffer buffer{};
            _state = decodeFrame(avframe, buffer);
            if (_state < 0)
                break;

            buffers.push_back(buffer);

            //if(buffers.size() > 30)
            {
                std::unique_lock<std::mutex> lock(_mtxRead, std::try_to_lock);
                if (sessionIndex != _sessionIndex)
                    break;

                if (lock.owns_lock() && _buffers.size() < _minFrames)
                {
                    _buffers.splice(_buffers.end(), buffers);
                    lock.unlock();
                    _condRead.notify_all();
                }
            }
        }

        std::unique_lock<std::mutex> lock(_mtxRead);
        _condDecoder.wait(lock, [this, &sessionIndex] {return _buffers.size() < _minFrames || _state < 0 || sessionIndex != _sessionIndex; });

        //重建资源
        if (sessionIndex != _sessionIndex)
        {
            sessionIndex = _sessionIndex;

            _swr.reset(swr_alloc(), swr_free_wrap);
            int64_t in_channel_layout = av_get_default_channel_layout(_inputFormat.chanels);
            int64_t out_channel_layout = av_get_default_channel_layout(_outputFormat.chanels);

            av_opt_set_int(_swr.get(), "in_channel_layout", in_channel_layout, 0);
            av_opt_set_int(_swr.get(), "in_sample_rate", _inputFormat.sampleRate, 0);
            av_opt_set_sample_fmt(_swr.get(), "in_sample_fmt", _inputFormat.sampleFormat, 0);

            av_opt_set_int(_swr.get(), "out_channel_layout", out_channel_layout, 0);
            av_opt_set_int(_swr.get(), "out_sample_rate", _outputFormat.sampleRate, 0);
            av_opt_set_sample_fmt(_swr.get(), "out_sample_fmt", _outputFormat.sampleFormat, 0);

            averr = swr_init(_swr.get());
            if (averr < 0)
            {
                _state = FpStateBadState;
                return;
            }

            for (auto iter = buffers.begin(); iter != buffers.end(); ++iter)
                _sessionFrames.push_front(iter->avframe);
            buffers.clear();
        }
        else
        {
            _buffers.splice(_buffers.end(), buffers);
            lock.unlock();
            _condRead.notify_all();
        }
    }
}
