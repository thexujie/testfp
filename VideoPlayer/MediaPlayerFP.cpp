#include "stdafx.h"
#include "MediaPlayerFP.h"

#define DEMUXER_SYNC 0
#define DECODE_SYNC 1

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

MediaDemuxerFP::MediaDemuxerFP()
{

}

MediaDemuxerFP::~MediaDemuxerFP()
{
    _thRead.join();
}

FpError MediaDemuxerFP::LoadFromFile(const u8string & filePath)
{
    int32_t averr = 0;

    AVFormatContext * avformatContext = nullptr;
    averr = avformat_open_input(&avformatContext, filePath.c_str(), NULL, NULL);
    if (averr || !avformatContext)
        return FpErrorGeneric;

    _avformatContext.reset(avformatContext, avformat_free_context_2);

    //av_dump_format(m_avformatContext.get(), 0, filePath.c_str(), false);

    if (avformat_find_stream_info(_avformatContext.get(), NULL) < 0)
        return FpErrorGeneric;

    for (int32_t ist = 0; ist < _avformatContext->nb_streams; ++ist)
        _packets[ist];


    {
        //int32_t streamId = 1;
        //std::shared_ptr<AVCodecContext> _codec;
        //if (!_codec)
        //{
        //    FpAudioFormat inputFormat = GetAudioFormat(streamId);
        //    AVCodec * avcodec = avcodec_find_decoder(inputFormat.codecId);
        //    if (!avcodec)
        //    {
        //        _readEnd = true;
        //        return {};
        //    }

        //    _codec.reset(avcodec_alloc_context3(avcodec), avcodec_free_context_wrap);
        //    _codec->sample_fmt = inputFormat.sampleFormat;
        //    _codec->channels = inputFormat.chanels;
        //    //_codec->channel_layout = av_get_default_channel_layout(_inputFormat.chanels);;
        //    _codec->sample_rate = inputFormat.sampleRate;

        //    if (avcodec_open2(_codec.get(), avcodec, NULL) < 0)
        //    {
        //        _readEnd = true;
        //        return {};
        //    }
        //}


        //AVPacket * avpacket = 0;
        //if (!avpacket)
        //{
        //    avpacket = av_packet_alloc();
        //    av_init_packet(avpacket);
        //}

        ////需要读取一个包
        //while (true)
        //{
        //    if (avpacket->duration <= 0)
        //    {
        //        averr = av_read_frame(_avformatContext.get(), avpacket);
        //        if (averr == AVERROR_EOF)
        //        {
        //            //没了
        //            av_packet_unref(avpacket);
        //            break;
        //        }

        //        if (averr)
        //        {
        //            log(m_logFile, "av_read_frame %d.\n", averr);
        //            av_packet_unref(avpacket);
        //            break;
        //        }

        //        //if(udata.avpacket->dts == AV_NOPTS_VALUE)
        //        //{
        //        //    av_packet_unref(udata.avpacket);
        //        //    continue;
        //        //}

        //        if (avpacket->stream_index != 1)
        //        {
        //            av_packet_unref(avpacket);
        //            continue;
        //        }

        //        printf("packet [%lld] pos=%lld, pts=%lld, dur=%lld, size=%d.\n", 0i64, avpacket->pos, avpacket->pts, avpacket->duration, avpacket->size);
        //        averr = avcodec_send_packet(udata.context.avcodecContext, avpacket);
        //        if (averr)
        //        {
        //            log(m_logFile, "avcodec_send_packet %d.\n", averr);
        //            udata.state = audio_play_state_error;
        //            av_packet_unref(udata.avpacket);
        //            break;
        //        }

        //        udata.context.sampleIndex = -1;
        //    }

        //    //帧首
        //    if (udata.context.sampleIndex < 0)
        //    {
        //        if (!udata.avframe)
        //            udata.avframe = av_frame_alloc();

        //        averr = avcodec_receive_frame(udata.context.avcodecContext, udata.avframe);
        //        if (averr == AVERROR(EAGAIN))
        //        {
        //            //读取下一个 packetav_free_packet(&packet);
        //            av_frame_unref(udata.avframe);
        //            av_packet_unref(udata.avpacket);
        //            continue;
        //        }

        //        if (averr < 0)
        //        {
        //            log(m_logFile, "avcodec_receive_frame %d.\n", averr);
        //            //udata.state = audio_play_state_error;
        //            //break;
        //            //读取下一个 packet
        //            // QQ Music 转码后，ff_flac_decode_frame_header 会出现末尾解包错误。
        //            av_frame_unref(udata.avframe);
        //            av_packet_unref(udata.avpacket);
        //            continue;
        //        }
        //        ++udata.frameIndex;
        //        udata.context.sampleIndex = udata.sampleIndex;
        //    }
        //    av_frame_unref(udata.avframe);
        //    av_packet_unref(udata.avpacket);
        //    continue;
    }
    return FpErrorOK;
}

FpError MediaDemuxerFP::State(int32_t streamId) const
{
    auto iter = _packets.find(streamId);
    if (iter == _packets.end())
        return FpErrorNoData;

    return iter->second.state;
}

std::map<int32_t, AVMediaType> MediaDemuxerFP::GetStreamTypes() const
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

FpAudioFormat MediaDemuxerFP::GetAudioFormat(int32_t streamId) const
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

FpPacket MediaDemuxerFP::NextPacket(int32_t streamId)
{
#if DEMUXER_SYNC
    if(_thRead.get_id() == std::thread::id())
        _thRead = std::thread(std::bind(&MediaDemuxerFP::demuxerThread, this));

    std::unique_lock<std::mutex> lock(_mtxRead);
    if (_packets.find(streamId) == _packets.end())
        return {};

    _condRead.wait(lock, [this, streamId] {return !_packets[streamId].queue.empty() || _state < 0; });

    if (_packets[streamId].queue.empty() || _state < 0)
        return {};

    FpPacket packet = _packets[streamId].queue.front();
    _packets[streamId].queue.pop();

    if (_packets[streamId].queue.size() < _minPackets)
        _condDemuxer.notify_one();

    return packet;

#else
    auto & packets = _packets[streamId];
    while (packets.queue.size() < _maxPackets && _state >= 0)
    {
        std::shared_ptr<AVPacket> avpacket(av_packet_alloc(), av_packet_free_wap);
        av_init_packet(avpacket.get());
        int32_t averr = av_read_frame(_avformatContext.get(), avpacket.get()); if (averr == AVERROR_EOF)
        {
            _state = FpErrorEOF;
            break;
        }

        if (avpacket->stream_index != streamId)
            continue;

        if (!averr)
        {
            packets.queue.push({ avpacket, _packetIndex++, packets.index });
            ++packets.index;
        }
        else
        {
            _state = FpErrorGeneric;
            log("av_read_frame %d.\n", averr);
            break;
        }
    }

    if (_packets[streamId].queue.empty() || _state < 0)
        return {};

    FpPacket packet = _packets[streamId].queue.front();
    _packets[streamId].queue.pop();
    return packet;
#endif
}

void MediaDemuxerFP::demuxerThread()
{
    thread_set_name(0, "demuxerThread");

    if (!_avformatContext)
    {
        _state = FpErrorBadState;
        return;
    }
    
    //std::this_thread::sleep_for(std::chrono::seconds(2));
    int32_t averr = 0;
    while (!_state)
    {
        std::unique_lock<std::mutex> lock(_mtxRead);
        _condDemuxer.wait(lock,
            [this]
        {
            for (auto iter = _packets.begin(); iter != _packets.end(); ++iter)
            {
                if (iter->second.queue.size() < _minPackets)
                    return true;
            }
            return false;
        }
            );

        for (auto iter = _packets.begin(); iter != _packets.end(); ++iter)
        {
            auto & packets = iter->second;
            while (packets.queue.size() < _maxPackets && _state >= 0)
            {
                std::shared_ptr<AVPacket> avpacket(av_packet_alloc(), av_packet_unref);
                av_init_packet(avpacket.get());
                averr = av_read_frame(_avformatContext.get(), avpacket.get());
                if (averr == AVERROR_EOF)
                {
                    //没了
                    _state = FpErrorEOF;;
                    break;
                }

                if (averr)
                {
                    _state = FpErrorGeneric;;
                    log("av_read_frame %d.\n", averr);
                }
                else
                {
                    int32_t streamId = avpacket->stream_index;
                    _packets[streamId].queue.push({ avpacket, _packetIndex++, _packets[streamId].index });
                    ++_packets[streamId].index;
                }
            }
        }

        lock.unlock();
        _condRead.notify_all();
    }
}


AudioDecoderFP::AudioDecoderFP(std::shared_ptr<IFFmpegDemuxer> demuxer, int32_t streamIndex)
    : _demuxer(demuxer)
, _streamIndex(streamIndex)
{
    _inputFormat = _demuxer->GetAudioFormat(_streamIndex);
}

AudioDecoderFP::~AudioDecoderFP()
{
    
}

std::shared_ptr<IFFmpegDemuxer> AudioDecoderFP::Demuxer() const
{
    return _demuxer;
}

int32_t AudioDecoderFP::StreamIndex() const
{
    return _streamIndex;
}

FpAudioFormat AudioDecoderFP::GetOutputFormat() const
{
    if (!_demuxer)
        return {};

    return _demuxer->GetAudioFormat(_streamIndex);
}

FpAudioBuffer AudioDecoderFP::NextBuffer()
{
    if (_outputFormat.sampleFormat == AV_SAMPLE_FMT_NONE)
        return {};

#if DECODE_SYNC
    if(_thread.get_id() == std::thread::id())
        _thread = std::thread(std::bind(&AudioDecoderFP::decoderThread, this));

    {
        std::lock_guard<std::mutex> lock(_mtxRead);
        if (!_buffers.empty())
        {
            FpAudioBuffer buffer = _buffers.front();
            _buffers.pop_front();

            if (_state >= 0 && _buffers.size() < _minFrames)
                _condDecoder.notify_one();

            return buffer;
        }
    }
    {
        std::unique_lock<std::mutex> lock(_mtxRead);
        _condDecoder.notify_one();
        _condRead.wait(lock, [this] {return !_buffers.empty() || _state < 0; });

        if (_buffers.empty())
            return {};

        FpAudioBuffer buffer = _buffers.front();
        _buffers.pop_front();

        if (_state >= 0 && _buffers.size() < _minFrames)
            _condDecoder.notify_one();

        return buffer;
    }
#else
    if(!_codec)
    {
        AVCodec * avcodecAudio = avcodec_find_decoder(_inputFormat.codecId);
        if (!avcodecAudio)
        {
            _readEnd = true;
            return {};
        }
        _codec.reset(avcodec_alloc_context3(avcodecAudio), avcodec_free_context_wrap);
        _codec->sample_fmt = _inputFormat.sampleFormat;
        _codec->channels = _inputFormat.chanels;
        //_codec->channel_layout = av_get_default_channel_layout(_inputFormat.chanels);;
        _codec->sample_rate = _inputFormat.sampleRate;

        if (avcodec_open2(_codec.get(), avcodecAudio, NULL) < 0)
        {
            _readEnd = true;
            return {};
        }
    }

    int32_t averr = 0;

    while (_frames.size() < _maxFrames && _state >= 0)
    {
        std::shared_ptr<AVFrame> avframe(av_frame_alloc(), av_frame_free_wrap);
        averr = avcodec_receive_frame(_codec.get(), avframe.get());
        if (!averr)
        {
            _frames.push({ avframe, _frameIndex++, 0 });
            continue;
        }

        if (averr == AVERROR(EAGAIN))
        {
            FpPacket _packet = _demuxer->NextPacket(_streamIndex);
            if (!_packet.ptr)
            {
                _state = FpErrorEOF;
                break;
            }

            if (_packet.ptr->stream_index != _streamIndex)
            {
                _state = FpErrorEOF;
                break;
            }

            printf("packet [%lld] pos=%lld, pts=%lld, dur=%lld.\n", _packet.localIndex, _packet.ptr->pos, _packet.ptr->pts, _packet.ptr->duration);

            averr = avcodec_send_packet(_codec.get(), _packet.ptr.get());
            if (averr < 0)
            {
                _state = FpErrorEOF;
                break;
            }
        }
        else
        {
            _state = FpErrorEOF;
            break;
        }
    }

    if (_frames.empty() || _state < 0)
        return {};

    FpFrame frame = _frames.front();
    _frames.pop();
    return frame;
#endif
}

FpError AudioDecoderFP::ResetFormat(FpAudioFormat format)
{
    _outputFormat = format;
    return FpErrorOK;
}

void AudioDecoderFP::decoderThread()
{
    thread_set_name(0, "decoderThread");
    thread_prom();

    if (!_demuxer || !_inputFormat.codecId || _outputFormat.sampleFormat == AV_SAMPLE_FMT_NONE)
    {
        _state = FpErrorBadState;
        return;
    }

    int32_t averr = 0;
    if (!_codec)
    {
        AVCodec * avcodec = avcodec_find_decoder(_inputFormat.codecId);
        if (!avcodec)
        {
            _state = FpErrorBadState;
            return;
        }
        _codec.reset(avcodec_alloc_context3(avcodec), avcodec_free_context_wrap);
        _codec->sample_fmt = _inputFormat.sampleFormat;
        _codec->channels = _inputFormat.chanels;
        //_codec->channel_layout = av_get_default_channel_layout(_inputFormat.chanels);;
        _codec->sample_rate = _inputFormat.sampleRate;

        if (avcodec_open2(_codec.get(), avcodec, NULL) < 0)
        {
            _state = FpErrorBadState;
            return;
        }
    }

    if (!_swr)
    {
        int64_t in_channel_layout = av_get_default_channel_layout(_inputFormat.chanels);
        int64_t out_channel_layout = av_get_default_channel_layout(_outputFormat.chanels);

#if LIBSWRESAMPLE_VERSION_MAJOR >= 3
        _swr.reset(swr_alloc(), swr_free_wrap);

        av_opt_set_int(_swr.get(), "in_channel_layout", in_channel_layout, 0);
        av_opt_set_int(_swr.get(), "in_sample_rate", _inputFormat.sampleRate, 0);
        av_opt_set_sample_fmt(_swr.get(), "in_sample_fmt", _inputFormat.sampleFormat, 0);

        av_opt_set_int(_swr.get(), "out_channel_layout", out_channel_layout, 0);
        av_opt_set_int(_swr.get(), "out_sample_rate", _outputFormat.sampleRate, 0);
        av_opt_set_sample_fmt(_swr.get(), "out_sample_fmt", _outputFormat.sampleFormat, 0);
#else
        _swr.reset(swr_alloc_set_opts(nullptr, out_channel_layout, _outputFormat.sampleFormat, _outputFormat.sampleRate,
            in_channel_layout, _inputFormat.sampleFormat, _inputFormat.sampleRate, 0, NULL), swr_free_wrap);
#endif
        averr = swr_init(_swr.get());
        if (averr < 0)
        {
            _state = FpErrorBadState;
            return;
        }
    }

    //-------------------------------------------------------------

    std::list<FpAudioBuffer> buffers;
    std::shared_ptr<AVFrame> avframe(av_frame_alloc(), av_frame_free_wrap);
    while (_state >= 0)
    {
        while (_state >= 0 && buffers.size() < _minFrames)
        {
            // receive a frame
            while (_state >= 0)
            {
                averr = avcodec_receive_frame(_codec.get(), avframe.get());
                if (!averr)
                    break;

                if (averr == AVERROR(EAGAIN))
                {
                    FpPacket _packet = _demuxer->NextPacket(_streamIndex);
                    if (!_packet.ptr)
                    {
                        _state = FpErrorBadState;
                        break;
                    }

                    if (_packet.ptr->stream_index != _streamIndex)
                    {
                        _state = FpErrorBadState;
                        break;
                    }

                    printf("packet [%lld] pos=%lld, pts=%lld, dur=%lld.\n", _packet.localIndex, _packet.ptr->pos, _packet.ptr->pts, _packet.ptr->duration);

                    averr = avcodec_send_packet(_codec.get(), _packet.ptr.get());
                    if (averr < 0)
                    {
                        _state = FpErrorBadState;
                        break;
                    }
                }
                else
                {
                    _state = FpErrorBadState;
                    break;
                }
            }


            FpAudioBuffer buffer{};
            uint32_t bytesPerSample = av_get_bytes_per_sample(_outputFormat.sampleFormat) * _outputFormat.chanels;
            buffer.numSamples = swr_get_out_samples(_swr.get(), avframe->nb_samples);
            buffer.data = std::shared_ptr<uint8_t>(static_cast<uint8_t *>(av_malloc(buffer.numSamples * bytesPerSample)), av_free);

            int64_t numSamples = 0;
            while(numSamples < buffer.numSamples)
            {
                uint8_t * dst = buffer.data.get() + numSamples * bytesPerSample;
                if(!numSamples)
                {
                    averr = swr_convert(_swr.get(), &dst, buffer.numSamples - numSamples, (const uint8_t **)avframe->data, avframe->nb_samples);
                    av_frame_unref(avframe.get());
                }
                else
                    averr = swr_convert(_swr.get(), &dst, buffer.numSamples - numSamples, 0, 0);

                if (averr == 0)
                    break;

                if (averr < 0)
                {
                    _state = FpErrorBadState;
                    break;
                }
                
                numSamples += averr;
            }

            buffer.numSamples = numSamples;
            buffers.push_back(buffer);
        }

        std::unique_lock<std::mutex> lock(_mtxRead);
        _condDecoder.wait(lock, [this] {return _buffers.size() < _minFrames || _state < 0; });
        _buffers.splice(_buffers.end(), buffers);
        _condRead.notify_all();
    }
}
