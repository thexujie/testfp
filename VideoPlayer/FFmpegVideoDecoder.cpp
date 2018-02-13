#include "stdafx.h"
#include "FFmpegVideoDecoder.h"

#define DEMUXER_ASYNCHRONOUS 1
#define DECODE_ASYNCHRONOUS 1

FFmpegVideoDecoder::FFmpegVideoDecoder(std::shared_ptr<IVideoPacketReader> reader)
    : _reader(reader)
{
    _inputParam = _reader->GetVideoDecodeParam();
}

FFmpegVideoDecoder::~FFmpegVideoDecoder()
{
    if (_thDecoder.joinable())
    {
        _flags = _flags | FpFlagStop;
        _condDecoder.notify_all();
        _thDecoder.join();
    }
}

std::shared_ptr<IVideoPacketReader> FFmpegVideoDecoder::Stream() const
{
    return _reader;
}

VideoFormat FFmpegVideoDecoder::GetOutputFormat() const
{
    return _outputFormat;
}

FpState FFmpegVideoDecoder::SetOutputFormat(VideoFormat format)
{
    if(format == _outputFormat)
        return FpStateOK;

    std::unique_lock<std::mutex> lock(_mtx);
    _outputFormat = format;
    ++_sessionIndex;
    for (auto iter = _buffers.begin(); iter != _buffers.end(); ++iter)
        _sessionFrames.push_front(iter->avframe);
    _buffers.clear();
    _condDecoder.notify_all();
    return FpStateOK;
}

FpState FFmpegVideoDecoder::Ready(int64_t timeoutMS)
{
    return WaitForFrames(timeoutMS);
}

FpState FFmpegVideoDecoder::WaitForFrames(int64_t timeoutMS)
{
    if (!timeoutMS)
        timeoutMS = _asyncTimeOutTime;

    std::unique_lock<std::mutex> lock(_mtx);

    if (_thDecoder.get_id() == std::thread::id())
        _thDecoder = std::thread(std::bind(&FFmpegVideoDecoder::decoderThread, this));

    if (_condRead.wait_for(lock, std::chrono::milliseconds(timeoutMS), [this] {return !_buffers.empty(); }))
        return FpStateOK;
    else
        return FpStateTimeOut;
}

FpState FFmpegVideoDecoder::PeekBuffer(VideoBuffer & buffer)
{
    if (_outputFormat.pixelFormat == AV_PIX_FMT_NONE)
        return FpStateNoData;

    if (_state < 0 && _state != FpStateEOF)
        return _state;

    if (_thDecoder.get_id() == std::thread::id())
    {
        _thDecoder = std::thread(std::bind(&FFmpegVideoDecoder::decoderThread, this));
        return FpStatePending;
    }

    std::unique_lock<std::mutex> lock(_mtx);
    if (_buffers.empty())
    {
        //badly...
        lock.unlock();
        if (_state == FpStateEOF)
            return FpStateEOF;

        _condDecoder.notify_one();
        return FpStatePending;
    }

    buffer = _buffers.front();
    _buffers.pop_front();
    lock.unlock();
    _condDecoder.notify_one();
    return FpStateOK;
}

FpState FFmpegVideoDecoder::NextBuffer(VideoBuffer & buffer, int64_t timeoutMS)
{
    if (!timeoutMS)
        timeoutMS = _asyncTimeOutTime;

    if (_outputFormat.pixelFormat == AV_PIX_FMT_NONE)
        return FpStateEOF;

    if (_state < 0 && _state != FpStateEOF)
        return _state;

    std::unique_lock<std::mutex> lock(_mtx);
    if (_thDecoder.get_id() == std::thread::id())
    {
        _thDecoder = std::thread(std::bind(&FFmpegVideoDecoder::decoderThread, this));
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

    if (_state == FpStateEOF)
        return FpStateEOF;

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

FpState FFmpegVideoDecoder::decodeFrame(std::shared_ptr<AVFrame> avframe, VideoBuffer & buffer)
{
    int32_t averr = 0;
    buffer.pitch = av_image_get_linesize(_outputFormat.pixelFormat, _outputFormat.width, 0);
    buffer.avframe = avframe;
    buffer.size = _outputFormat.height * buffer.pitch;
    buffer.data = std::shared_ptr<uint8_t>(new uint8_t[buffer.size], [](uint8_t * ptr) {delete[] ptr; });
    buffer.width = _outputFormat.width;
    buffer.height = _outputFormat.height;
    buffer.pts = avframe->pts == AV_NOPTS_VALUE ? std::numeric_limits<double_t>::quiet_NaN() : avframe->pts * av_q2d(_inputParam.timeBase);
    buffer.duration = _inputParam.fps.num && _inputParam.fps.den ? 1.0 / av_q2d(_inputParam.fps) : 0;

    uint8_t * dst[AV_NUM_DATA_POINTERS] = { buffer.data.get() };
    int linesize[AV_NUM_DATA_POINTERS] = { buffer.pitch };
    int height = sws_scale(_sws.get(), avframe->data, avframe->linesize, 0, avframe->height, dst, linesize);

    return FpStateOK;
}

FpState FFmpegVideoDecoder::readFrame(std::shared_ptr<AVFrame> & frame)
{
    if(!_sessionFrames.empty())
    {
        frame = _sessionFrames.back();
        _sessionFrames.pop_back();
        return FpStateOK;
    }

    int32_t averr = 0;
    frame.reset(av_frame_alloc(), [](AVFrame * ptr) { av_frame_free(&ptr); });
    // receive a frame
    while (_state >= 0)
    {
        averr = avcodec_receive_frame(_codec.get(), frame.get());
        if (!averr)
            break;

        if (averr == AVERROR(EAGAIN))
        {
            Packet packet{};
            _state = _reader->NextPacket(packet, std::numeric_limits<uint32_t>::max());

            if (_state < 0)
                break;

            if(!packet.ptr)
            {
                _state = FpStateEOF;
                break;
            }

            //printf("packet [%lld] pos=%lld, pts=%lld, dur=%lld.\n", packet.localIndex, packet.ptr->pos, packet.ptr->pts, packet.ptr->duration);

            averr = avcodec_send_packet(_codec.get(), packet.ptr.get());
            if (averr < 0)
            {
                _state = FpStateBadState;
                break;
            }
        }
        else
        {
            log(m_logFile, "avcodec_receive_frame %d.\n", averr);
            //udata.state = audio_play_state_error;
            //break;
            //读取下一个 packet
            // QQ Music 转码后，ff_flac_decode_frame_header 会出现末尾解包错误。
            //_state = FpStateBadState;
            //break;
        }
    }
    return _state;
}

void FFmpegVideoDecoder::decoderThread()
{
    thread_set_name(0, "videoDecoderThread");
    //thread_prom();

    if (!_reader || !_inputParam.codecId || _outputFormat.pixelFormat == AV_PIX_FMT_NONE)
    {
        _state = FpStateBadState;
        return;
    }

    int32_t averr = 0;
    if (!_codec)
    {
        AVCodec * avcodec = avcodec_find_decoder(_inputParam.codecId);
        if (!avcodec)
        {
            _state = FpStateBadState;
            return;
        }
        _codec.reset(avcodec_alloc_context3(avcodec), [](AVCodecContext * ptr) {avcodec_free_context(&ptr); });
        //std::shared_ptr<AudioPacketReaderFP> apr = std::dynamic_pointer_cast<AudioPacketReaderFP>(_reader);
        //std::shared_ptr<FFmpegDemuxerFP> demuxer = apr->Demuxer();
        //std::shared_ptr<AVCodecParameters> param = demuxer->GetCodecParameters(apr->StreamIndex());

        _codec->codec_type = AVMEDIA_TYPE_VIDEO;
        _codec->codec_id = _inputParam.codecId;
        _codec->codec_tag = _inputParam.codecTag;

        _codec->bit_rate = _inputParam.bitRate;
        _codec->bits_per_coded_sample = _inputParam.bitsPerCodedSample;
        _codec->bits_per_raw_sample = _inputParam.bitsPerRawSample;
        _codec->profile = _inputParam.profile;
        _codec->level = _inputParam.level;

        _codec->field_order = _inputParam.fieldOrder;
        _codec->color_range = _inputParam.colorRange;
        _codec->color_primaries = _inputParam.colorPrimaries;
        _codec->color_trc = _inputParam.colorTrc;
        _codec->colorspace = _inputParam.colorSpace;
        _codec->chroma_sample_location = _inputParam.chromaLocation;
        _codec->sample_aspect_ratio = _inputParam.aspectRatio;
        _codec->has_b_frames = _inputParam.hasBFrames;

        if(_inputParam.extraDataSize > 0)
        {
            _codec->extradata = (uint8_t *)av_mallocz(_inputParam.extraDataSize + AV_INPUT_BUFFER_PADDING_SIZE);
            if(!_codec->extradata)
                return;
            memcpy(_codec->extradata, _inputParam.extraData.get(), _inputParam.extraDataSize);
            _codec->extradata_size = _inputParam.extraDataSize;
        }
        //avcodec_parameters_to_context(_codec.get(), param.get());

        averr = avcodec_open2(_codec.get(), avcodec, NULL);
        if (averr < 0)
        {
            _state = FpStateBadState;
            return;
        }
    }

    //-------------------------------------------------------------
    int64_t sessionIndex = _sessionIndex - 1;
    std::list<VideoBuffer> buffers;
    FpState error = FpStateOK;
    while (_state >= 0 && error >= 0)
    {
        while (_state >= 0 && error >= 0 && buffers.size() < 1 && sessionIndex == _sessionIndex)
        {
            std::shared_ptr<AVFrame> avframe;
            _state = readFrame(avframe);
            if (_state == FpStatePending)
                break;

            if (_state != 0)
                break;

            VideoBuffer buffer = {};
            _state = decodeFrame(avframe, buffer);
            if (_state < 0)
                break;

            buffers.push_back(buffer);

            //if(buffers.size() > 30)
            {
                std::unique_lock<std::mutex> lock(_mtx, std::try_to_lock);
                if (_flags & FpFlagStop)
                    break;

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

        std::unique_lock<std::mutex> lock(_mtx);
        _condDecoder.wait(lock, [this, &sessionIndex] {return _buffers.size() < _minFrames || _state < 0 || sessionIndex != _sessionIndex || (_flags & FpFlagStop); });
        if (_flags & FpFlagStop)
            break;

        //重建资源
        if (sessionIndex != _sessionIndex)
        {
            sessionIndex = _sessionIndex;
            lock.unlock();

            //_sws.reset(sws_alloc_context(), [](SwsContext * ptr) { sws_freeContext(ptr); });

            SwsContext * ctx = sws_getContext(_inputParam.format.width, _inputParam.format.height, _inputParam.format.pixelFormat,
                _outputFormat.width, _outputFormat.height, _outputFormat.pixelFormat, SWS_POINT, NULL, NULL, NULL);
            if(!ctx)
            {
                _state = FpStateBadState;
                return;
            }

            _sws.reset(ctx, [](SwsContext * ptr) { sws_freeContext(ptr); });

            for (auto iter = buffers.begin(); iter != buffers.end(); ++iter)
                _sessionFrames.push_front(iter->avframe);
            buffers.clear();
            continue;
        }

        _buffers.splice(_buffers.end(), buffers);
        lock.unlock();
        _condRead.notify_all();
    }
}
