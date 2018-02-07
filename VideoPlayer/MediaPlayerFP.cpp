#include "stdafx.h"
#include "MediaPlayerFP.h"

void avformat_free_context_2(AVFormatContext *& ptr)
{
    avformat_free_context(ptr);
}

void av_packet_free_2(AVPacket * ptr)
{
    av_packet_free(&ptr);
}

void avcodec_free_context_wrap(AVCodecContext * ptr)
{
    avcodec_free_context(&ptr);
}

void av_frame_free_wrap(AVFrame * ptr)
{
    av_frame_free(&ptr);
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
    int averr = 0;

    AVFormatContext * avformatContext = nullptr;
    averr = avformat_open_input(&avformatContext, filePath.c_str(), NULL, NULL);
    if (averr || !avformatContext)
        return FpErrorGeneric;

    _avformatContext.reset(avformatContext, avformat_free_context_2);

    //av_dump_format(m_avformatContext.get(), 0, filePath.c_str(), false);

    if (avformat_find_stream_info(_avformatContext.get(), NULL) < 0)
        return FpErrorGeneric;

    for (int ist = 0; ist < _avformatContext->nb_streams; ++ist)
        _packets[ist];

    return FpErrorOK;
}

std::map<int, AVMediaType> MediaDemuxerFP::GetStreamTypes() const
{
    std::map<int, AVMediaType> types;
    if (_avformatContext)
    {
        for (int ist = 0; ist < _avformatContext->nb_streams; ++ist)
        {
            types[ist] = _avformatContext->streams[ist]->codecpar->codec_type;
        }
    }
    return types;
}

FpAudioFormat MediaDemuxerFP::GetAudioFormat(int streamId) const
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
    format.numBuffers = 0;
    format.codecId = avcodecAudioParameters->codec_id;
    return format;
}

FpPacket MediaDemuxerFP::NextPacket(int streamId)
{
    //if(_thRead.get_id() == std::thread::id())
    //    _thRead = std::thread(std::bind(&MediaDemuxerFP::demuxerThread, this));

    while(true)
    {
        {
            std::lock_guard<std::mutex> lock(_mtxRead);
            if (!_packets[streamId].queue.empty())
            {
                FpPacket packet = _packets[streamId].queue.front();
                if (_packets[streamId].queue.size() < _numMinPackets)
                    _condDemuxer.notify_one();

                _packets[streamId].queue.pop();
                return packet;
            }
            if (_readEnd)
                return { nullptr, -1 };
        }

        //_condDemuxer.notify_one();

        //{
        //    std::unique_lock<std::mutex> lock(_mtxRead);
        //    _condRead.wait(lock);
        //}


        for (auto iter = _packets.begin(); iter != _packets.end(); ++iter)
        {
            auto & packets = iter->second;
            while (packets.queue.size() < _numMaxPackets)
            {
                std::shared_ptr<AVPacket> avpacket(av_packet_alloc(), av_packet_free_2);
                av_init_packet(avpacket.get());
                int averr = av_read_frame(_avformatContext.get(), avpacket.get());
                if (averr == AVERROR_EOF)
                {
                    //没了
                    _readEnd = true;
                    break;
                }

                if (averr)
                {
                    log("av_read_frame %d.\n", averr);
                    break;
                }

                _packets[avpacket->stream_index].queue.push({ avpacket, _packetIndex++, _packets[avpacket->stream_index].index });
                ++_packets[avpacket->stream_index].index;
            }
        }
    }
}

void MediaDemuxerFP::demuxerThread()
{
    if (!_avformatContext)
        return;
    
    //std::this_thread::sleep_for(std::chrono::seconds(2));
    int averr = 0;
    while (!_readEnd)
    {
        printf("demuxerThread\n");

        {
            std::lock_guard<std::mutex> lock(_mtxRead);
            for (auto iter = _packets.begin(); iter != _packets.end(); ++iter)
            {
                auto & packets = iter->second;
                while (packets.queue.size() < _numMaxPackets)
                {
                    std::shared_ptr<AVPacket> avpacket(av_packet_alloc(), av_packet_free_2);
                    av_init_packet(avpacket.get());
                    averr = av_read_frame(_avformatContext.get(), avpacket.get());
                    if (averr == AVERROR_EOF)
                    {
                        //没了
                        _readEnd = true;
                        break;
                    }

                    if (averr)
                    {
                        log("av_read_frame %d.\n", averr);
                        break;
                    }

                    int streamId = avpacket->stream_index;
                    _packets[streamId].queue.push({ avpacket, _packetIndex++, _packets[streamId].index });
                    ++_packets[streamId].index;
                }
            }
        }

        _condRead.notify_all();

        {
            std::unique_lock<std::mutex> lock(_mtxRead);
            _condDemuxer.wait(lock);
        }
    }
}


AudioDecoderFP::AudioDecoderFP(std::shared_ptr<IFFmpegDemuxer> demuxer, int32_t streamIndex)
    : _demuxer(demuxer)
, _streamIndex(streamIndex)
{
    
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

FpFrame AudioDecoderFP::NextFrame()
{
    //if(_thread.get_id() == std::thread::id())
    //    _thread = std::thread(std::bind(&AudioDecoderFP::decoderThread, this));

#if 0
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(_mtxRead);
            if (!_frames.empty())
            {
                FpFrame frame = _frames.front();
                if (_frames.size() < _minFrames)
                    _condDemuxer.notify_one();

                _frames.pop();
                return frame;
            }

            if (_readEnd)
                return { nullptr, -1 };
        }

        _condDemuxer.notify_one();

        {
            std::unique_lock<std::mutex> lock(_mtxRead);
            _condRead.wait(lock);
        }
    }
#else
    {
        static std::shared_ptr<AVCodecContext> _codec;
        if (!_codec)
        {
            FpAudioFormat inputFormat = _demuxer->GetAudioFormat(_streamIndex);
            AVCodec * avcodecAudio = avcodec_find_decoder(inputFormat.codecId);
            if (!avcodecAudio)
            {
                _readEnd = true;
                return {};
            }

            _codec.reset(avcodec_alloc_context3(avcodecAudio), avcodec_free_context_wrap);
            _codec->sample_fmt = inputFormat.sampleFormat;
            _codec->channels = inputFormat.chanels;
            //_codec->channel_layout = av_get_default_channel_layout(_inputFormat.chanels);;
            _codec->sample_rate = _inputFormat.sampleRate;

            if (avcodec_open2(_codec.get(), avcodecAudio, NULL) < 0)
            {
                _readEnd = true;
                return {};
            }
        }

        static FpPacket _packet{};
        int averr = 0;
        while (_frames.size() < _maxFrames)
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
                _packet = _demuxer->NextPacket(_streamIndex);
                if (!_packet.ptr)
                {
                    _readEnd = true;
                    break;
                }

                if (_packet.ptr->stream_index != _streamIndex)
                {
                    _readEnd = true;
                    break;
                }
                printf("packet [%lld] pos=%lld, pts=%lld, dur=%lld.\n", _packet.localIndex, _packet.ptr->pos, _packet.ptr->pts, _packet.ptr->duration);

                averr = avcodec_send_packet(_codec.get(), _packet.ptr.get());
                if (averr < 0)
                {
                    char err[512];
                    av_strerror(averr, err, 512);
                    _readEnd = true;
                    break;
                }
            }
            else
            {
                _readEnd = true;
                break;
            }
        }

        if (_frames.empty())
            return {};

        FpFrame frame = _frames.front();
        _frames.pop();
        return frame;
    }
#endif
}

FpError AudioDecoderFP::ResetFormat(FpAudioFormat format)
{
    return FpErrorOK;
}

void AudioDecoderFP::decoderThread()
{
    if (!_demuxer)
        return;

    FpAudioFormat inputFormat = _demuxer->GetAudioFormat(_streamIndex);
    AVCodec * avcodecAudio = avcodec_find_decoder(inputFormat.codecId);
    if (!avcodecAudio)
    {
        _readEnd = true;
        return;
    }

    std::shared_ptr<AVCodecContext> _codec(avcodec_alloc_context3(avcodecAudio), avcodec_free_context_wrap);

    _codec->sample_fmt = inputFormat.sampleFormat;
    _codec->channels = inputFormat.chanels;
    //_codec->channel_layout = av_get_default_channel_layout(_inputFormat.chanels);;
    _codec->sample_rate = _inputFormat.sampleRate;

    if (avcodec_open2(_codec.get(), avcodecAudio, NULL) < 0)
    {
        _readEnd = true;
        return;
    }

    FpPacket _packet{};
    int averr = 0;
    while (!_readEnd)
    {
        printf("decoderThread\n");
        {
            std::lock_guard<std::mutex> lock(_mtxRead);
            while (_frames.size() < _maxFrames)
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
                    _packet.ptr.reset();
                    _packet = _demuxer->NextPacket(_streamIndex);
                    if (!_packet.ptr)
                    {
                        _readEnd = true;
                        break;
                    }

                    if(_packet.ptr->stream_index != _streamIndex)
                    {
                        _readEnd = true;
                        break;
                    }
                    printf("packet [%lld] pos=%lld, pts=%lld, dur=%lld.\n", _packet.localIndex, _packet.ptr->pos, _packet.ptr->pts, _packet.ptr->duration);

                    averr = avcodec_send_packet(_codec.get(), _packet.ptr.get());
                    if (averr < 0)
                    {
                        char err[512];
                        av_strerror(averr, err, 512);
                        _readEnd = true;
                        break;
                    }
                }
                else
                {
                    _readEnd = true;
                    break;
                }
            }
        }
        
        _condRead.notify_all();

        {
            std::unique_lock<std::mutex> lock(_mtxRead);
            _condDemuxer.wait(lock);
        }
    }
}
