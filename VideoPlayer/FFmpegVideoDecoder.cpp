#include "stdafx.h"
#include "FFmpegVideoDecoder.h"

#include <libavcodec/dxva2.h>

#define DEMUXER_ASYNCHRONOUS 1
#define DECODE_ASYNCHRONOUS 1


//static enum AVPixelFormat get_hw_format(AVCodecContext * ctx, const enum AVPixelFormat * pix_fmts)
//{
//    const enum AVPixelFormat *p;
//
//    for (p = pix_fmts; *p != -1; p++)
//    {
//        if (*p == hw_pix_fmt)
//            return *p;
//    }
//
//    fprintf(stderr, "Failed to get HW surface format.\n");
//    return AV_PIX_FMT_NONE;
//}


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

FpState FFmpegVideoDecoder::SetVideoFormatCooperator(std::shared_ptr<IVideoDecoderHWAccelerator> formatCooperator)
{
    std::unique_lock<std::mutex> lock(_mtx);
    _hwAccel = formatCooperator;
    return FpStateOK;
}

FpState FFmpegVideoDecoder::SetOutputFormat(VideoFormat format)
{
    if(format == _outputFormat)
        return FpStateOK;

    std::unique_lock<std::mutex> lock(_mtx);
    _outputFormat = format;
    ++_sessionIndex;
    _condDecoder.notify_all();
    return FpStateOK;
}

VideoFormat FFmpegVideoDecoder::DecodeFormat() const
{
    const_cast<FFmpegVideoDecoder *>(this)->initDecoder();
    return _decodeFormat;
}

VideoFormat FFmpegVideoDecoder::OutputFormat() const
{
    std::lock_guard<std::mutex> lock(const_cast<FFmpegVideoDecoder *>(this)->_mtx);
    return _outputFormat;
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
	_dts = buffer.avframe->pts;
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
        lock.unlock();
        _condDecoder.notify_one();
        _dts = buffer.avframe->pts;
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
    lock.unlock();
    _condDecoder.notify_one();
    _dts = buffer.avframe->pts;
    return FpStateOK;
}

std::tuple<int64_t, int64_t> FFmpegVideoDecoder::BufferQuality() const
{
    std::lock_guard<std::mutex> lock(const_cast<FFmpegVideoDecoder *>(this)->_mtx);
    return { _buffers.size(), _maxFrames };
}

AVPixelFormat FFmpegVideoDecoder::_cooperateFormat(AVCodecContext * codecContext, const AVPixelFormat * pixelFormats)
{
    if (!codecContext || !pixelFormats || *pixelFormats == AV_PIX_FMT_NONE)
        return AV_PIX_FMT_NONE;

    if (!codecContext->opaque)
        return pixelFormats[0];

    FFmpegVideoDecoder * decoder = static_cast<FFmpegVideoDecoder *>(codecContext->opaque);
    return decoder->cooperateFormat(codecContext, pixelFormats);
}

int32_t FFmpegVideoDecoder::_hwaccelGetBuffer(AVCodecContext * codecContext, AVFrame * frame, int32_t flags)
{
    if (!codecContext || !frame)
        return AV_PIX_FMT_NONE;

    if (!codecContext->opaque)
        return avcodec_default_get_buffer2(codecContext, frame, flags);

    FFmpegVideoDecoder * decoder = static_cast<FFmpegVideoDecoder *>(codecContext->opaque);
    return decoder->hwaccelGetBuffer(codecContext, frame, flags);
}

AVPixelFormat FFmpegVideoDecoder::cooperateFormat(AVCodecContext * codecContext, const AVPixelFormat * pixelFormats)
{
    if (!pixelFormats || !codecContext || *pixelFormats == AV_PIX_FMT_NONE)
        return AV_PIX_FMT_NONE;

    if(!_hwAccel || !_hwAccellerator)
        return avcodec_default_get_format(codecContext, pixelFormats);


    bool surpportFormat = false;
    const AVPixelFormat * pixFormatsCurr = pixelFormats;
    while (*pixFormatsCurr != AV_PIX_FMT_NONE)
    {
        if (*pixFormatsCurr++ == _hwAccelPixelFormat)
        {
            surpportFormat = true;
            break;
        }
    }
    if(!surpportFormat)
        return avcodec_default_get_format(codecContext, pixelFormats);

    // 不合适
    if (!codecContext->hwaccel_context)
    {
        codecContext->hwaccel_context = _hwAccellerator->GetFFmpegHWAccelContext();
        assert(codecContext->hwaccel_context);
        if (!codecContext->hwaccel_context)
        {
            std::unique_lock<std::mutex> lock(_mtx);
            _hwAccelDeviceType = AV_HWDEVICE_TYPE_NONE;
            _hwAccelPixelFormat = AV_PIX_FMT_NONE;
            _hwAccellerator.reset();
        }
        else
        {

        }
    }
    return _hwAccelPixelFormat;

    if (!codecContext->hw_device_ctx)
    {
        std::shared_ptr<AVBufferRef> hwDeviceContext(av_hwdevice_ctx_alloc(_hwAccelDeviceType), [](AVBufferRef * ptr) {av_buffer_unref(&ptr); });
        int averr = av_hwdevice_ctx_init(hwDeviceContext.get());
        if (averr >= 0)
        {
            _hwDeviceContext = hwDeviceContext;
            AVHWDeviceContext * ptr = reinterpret_cast<AVHWDeviceContext *>(hwDeviceContext->data);
            ptr->hwctx = _hwAccellerator->GetFFmpegHWDeviceContext();

            //    assert(codecContext->hwaccel_context);
            //    if (!codecContext->hwaccel_context)
            //    {
            //        std::unique_lock<std::mutex> lock(_mtx);
            //        _hwAccelDeviceType = AV_HWDEVICE_TYPE_NONE;
            //        _hwAccelPixelFormat = AV_PIX_FMT_NONE;
            //        _hwAccellerator.reset();
            //    }
            //    else
            //    {

            //    }

            codecContext->hw_device_ctx = hwDeviceContext.get();
        }
        else
        {
            std::unique_lock<std::mutex> lock(_mtx);
            _hwAccelDeviceType = AV_HWDEVICE_TYPE_NONE;
            _hwAccelPixelFormat = AV_PIX_FMT_NONE;
            _hwAccellerator.reset();
        }

        //codecContext->hwaccel_context = _hwAccellerator->GetFFmpegHWAccelContext();
        //assert(codecContext->hwaccel_context);
        //if (!codecContext->hwaccel_context)
        //{
        //    _hwDeviceContext = hwDeviceContext;
        //    AVHWDeviceContext * ptr = reinterpret_cast<AVHWDeviceContext *>(hwDeviceContext->data);
        //    codecContext->hw_device_ctx = hwDeviceContext.get();

        //    std::unique_lock<std::mutex> lock(_mtx);
        //    _hwAccelDeviceType = AV_HWDEVICE_TYPE_NONE;
        //    _hwAccelPixelFormat = AV_PIX_FMT_NONE;
        //    _hwAccellerator.reset();
        //}
        //else
        //{

        //}


    }

    return _hwAccelPixelFormat;
}


int32_t FFmpegVideoDecoder::hwaccelGetBuffer(AVCodecContext * codecContext, AVFrame * frame, int32_t flags) const
{
    if(!_hwAccellerator)
        return avcodec_default_get_buffer2(codecContext, frame, flags);

    std::shared_ptr<AVFrame> avframe(frame, [](void *) {});
    return _hwAccellerator->GetBuffer(avframe, flags);
}

FpState FFmpegVideoDecoder::initDecoder()
{
    std::lock_guard<std::mutex> lock(_mtx);

    if (_codecContext)
        return FpStateOK;

    int32_t averr = 0;
    AVCodec * codec = avcodec_find_decoder(_inputParam.codecId);
    //AVCodec * codec = DXVADecoder::GetCodec();
    //AVCodec * codec = avcodec_find_decoder_by_name("h264_cuvid");
    //AVCodec * codec = avcodec_find_decoder_by_name("h264_dxva2");
    if (!codec)
    {
        _state = FpStateBadState;
        return FpStateBadState;
    }
    printf("use decoder %s[%s].\n", codec->name, codec->long_name);
    _codecContext.reset(avcodec_alloc_context3(codec), [](AVCodecContext * ptr) {avcodec_free_context(&ptr); });
    //std::shared_ptr<AudioPacketReaderFP> apr = std::dynamic_pointer_cast<AudioPacketReaderFP>(_reader);
    //std::shared_ptr<FFmpegDemuxerFP> demuxer = apr->Demuxer();
    //std::shared_ptr<AVCodecParameters> param = demuxer->GetCodecParameters(apr->StreamIndex());

    _codecContext->opaque = static_cast<void *>(this);
    _codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    _codecContext->codec_id = codec->id;
    _codecContext->codec_tag = _inputParam.codecTag;

    _codecContext->bit_rate = _inputParam.bitRate;
    _codecContext->bits_per_coded_sample = _inputParam.bitsPerCodedSample;
    _codecContext->bits_per_raw_sample = _inputParam.bitsPerRawSample;
    _codecContext->profile = _inputParam.profile;
    _codecContext->level = _inputParam.level;

    _codecContext->pix_fmt = _inputParam.format.pixelFormat;
    _codecContext->width = _inputParam.format.width;
    _codecContext->height = _inputParam.format.height;

    _codecContext->field_order = _inputParam.fieldOrder;
    _codecContext->color_range = _inputParam.colorRange;
    _codecContext->color_primaries = _inputParam.colorPrimaries;
    _codecContext->color_trc = _inputParam.colorTrc;
    _codecContext->colorspace = _inputParam.colorSpace;
    _codecContext->chroma_sample_location = _inputParam.chromaLocation;
    _codecContext->sample_aspect_ratio = _inputParam.aspectRatio;
    _codecContext->has_b_frames = _inputParam.hasBFrames;

    if (_inputParam.extraDataSize > 0)
    {
        _codecContext->extradata = (uint8_t *)av_mallocz(_inputParam.extraDataSize + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!_codecContext->extradata)
            return FpStateOutOfMemory;

        memcpy(_codecContext->extradata, _inputParam.extraData.get(), _inputParam.extraDataSize);
        _codecContext->extradata_size = _inputParam.extraDataSize;
    }
    //avcodec_parameters_to_context(_codec.get(), param.get());

    // 内置硬件加速
    if(_hwAccelDeviceTypeDefault != AV_HWDEVICE_TYPE_NONE)
    {
        AVPixelFormat hwAccelPixelFormat = AV_PIX_FMT_NONE;
        for (int cnt = 0;; cnt++)
        {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, cnt);
            if (config && config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == _hwAccelDeviceTypeDefault)
            {
                hwAccelPixelFormat = config->pix_fmt;
                break;
            }
        }

        AVBufferRef * hwAccelDeviceContext = nullptr;
        int err = av_hwdevice_ctx_create(&hwAccelDeviceContext, _hwAccelDeviceTypeDefault, NULL, NULL, 0);
        if(hwAccelDeviceContext)
        {
            _hwAccelDeviceType = _hwAccelDeviceTypeDefault;
            _hwAccelPixelFormat = hwAccelPixelFormat;
            _codecContext->hw_device_ctx = hwAccelDeviceContext;
            _codecContext->pix_fmt = hwAccelPixelFormat;
        }
    }
    else if(_hwAccel)
    {
        std::vector<AVHWDeviceType> hwDeviceTypes;
        AVHWDeviceType temp = AV_HWDEVICE_TYPE_NONE;
        while ((temp = av_hwdevice_iterate_types(temp)) != AV_HWDEVICE_TYPE_NONE)
            hwDeviceTypes.emplace_back(temp);

        VideoCodecFormat codecFormat = { _codecContext->codec_id, _inputParam.format };
        if(!_hwAccellerator)
        {
            auto[hwAccelDeviceType, hwAccellerator] = _hwAccel->CreateAccelerator(hwDeviceTypes, codecFormat);
            if (hwAccelDeviceType != AV_HWDEVICE_TYPE_NONE)
            {
                AVPixelFormat hwAccelPixelFormat = hwAccellerator->GetOutputPixelFormat();
                if (hwAccelPixelFormat != AV_PIX_FMT_NONE)
                {
                    _hwAccellerator = hwAccellerator;

                    _hwAccelDeviceType = hwAccelDeviceType;
                    _hwAccelPixelFormat = hwAccelPixelFormat;

                    _codecContext->get_format = _cooperateFormat;
                    _codecContext->get_buffer2 = _hwaccelGetBuffer;
                    //_codecContext->thread_safe_callbacks = 1;
                    _codecContext->pix_fmt = hwAccelPixelFormat;

					CodecDeviceDesc deviceDesc = _hwAccel->GetCodecDeviceDesc();
					std::vector<CodecDesc> codecDescs = _hwAccel->GetCodecDescs(codecFormat.codecId);
					printf("use hwAccel [%s]\n", deviceDesc.deviceDescription.c_str());
                }
            }
        }
        else
        {
            _hwAccellerator->SetCodecFormat(codecFormat);
        }
    }
    else
    {
        //软解
    }

    _codecContext->thread_count = 4;
    _codecContext->thread_type = FF_THREAD_FRAME;

    averr = avcodec_open2(_codecContext.get(), codec, NULL);
    if(averr < 0)
    {
        _state = FpStateBadState;
        return FpStateBadState;
    }

    _decodeFormat.width = _codecContext->width;
    _decodeFormat.height = _codecContext->height;
    _decodeFormat.pixelFormat = _codecContext->pix_fmt;

    //默认格式
    if(_outputFormat.pixelFormat == AV_PIX_FMT_NONE)
        _outputFormat = _decodeFormat;

    return FpStateOK;
}

FpState FFmpegVideoDecoder::scaleFrame(std::shared_ptr<AVFrame> avframe, VideoBuffer & buffer)
{
    assert(avframe->format == _outputFormat.pixelFormat);
    if (avframe->format != _outputFormat.pixelFormat)
    {
        return FpStateInner;
    }

    buffer.index = _index++;
    buffer.width = _outputFormat.width;
    buffer.height = _outputFormat.height;
    int64_t pts = avframe->best_effort_timestamp;
    buffer.pts = avframe->pts == AV_NOPTS_VALUE ? std::numeric_limits<double_t>::quiet_NaN() : avframe->pts * av_q2d(_inputParam.timeBase);
    buffer.spf = _inputParam.fps.num && _inputParam.fps.den ? 1.0 / av_q2d(_inputParam.fps) : 0;
    buffer.dtc = (int64_t)avframe->opaque / (double_t)AV_TIME_BASE;

    const AVPixFmtDescriptor * pixelFormatDesc = av_pix_fmt_desc_get((AVPixelFormat)avframe->format);
    if (!pixelFormatDesc)
        return FpStateInner;

    // 硬件加速
    if (pixelFormatDesc->flags & AV_PIX_FMT_FLAG_HWACCEL)
    {
        //std::shared_ptr<AVFrame> tmpframe(av_frame_alloc(), [](AVFrame * ptr) { av_frame_free(&ptr); });
        //int32_t averr = av_hwframe_transfer_data(tmpframe.get(), avframe.get(), 0);
        //if(averr < 0)
        //    return FpStateInner;
        //buffer.avframe = tmpframe;
        buffer.avframe = avframe;
    }
    // 直接复制即可
    else if(_decodeFormat == _outputFormat)
    {
        //uint8_t * pointers[VideoBuffer::MAX_PLANE] = {};
        //int32_t linesizes[VideoBuffer::MAX_PLANE] = {};
        //av_image_alloc(pointers, linesizes, _outputFormat.width, _outputFormat.height, _outputFormat.pixelFormat, 1);
        buffer.avframe = avframe;
        //av_image_copy(pointers, linesizes, const_cast<const uint8_t **>(avframe->data), avframe->linesize, _outputFormat.pixelFormat, _outputFormat.width, _outputFormat.height);
    }
    else
    {
        if (!_sws)
        {
            SwsContext * ctx = sws_getContext(_decodeFormat.width, _decodeFormat.height, _decodeFormat.pixelFormat,
                _outputFormat.width, _outputFormat.height, _outputFormat.pixelFormat, SWS_POINT, NULL, NULL, NULL);
            if (!ctx)
            {
                _state = FpStateBadState;
                return FpStateBadState;
            }
            _sws.reset(ctx, [](SwsContext * ptr) { sws_freeContext(ptr); });
        }

        std::shared_ptr<AVFrame> avframe2(av_frame_alloc(), [](AVFrame * ptr) { av_frame_free(&ptr); });
        av_image_alloc(avframe2->data, avframe2->linesize, _outputFormat.width, _outputFormat.height, _outputFormat.pixelFormat, 1);

        int height = sws_scale(_sws.get(), avframe->data, avframe->linesize, 0, avframe->height, avframe2->data, avframe2->linesize);
        assert(height == _outputFormat.height);
        buffer.avframe = avframe2;
    }
    return FpStateOK;
}

FpState FFmpegVideoDecoder::decodeFrame(std::shared_ptr<AVFrame> & frame)
{
    //int64_t tsStart = av_gettime_relative();
    int32_t averr = 0;
    frame.reset(av_frame_alloc(), [](AVFrame * ptr) { av_frame_free(&ptr); });
    // receive a frame
    while (_state >= 0)
    {
        averr = avcodec_receive_frame(_codecContext.get(), frame.get());

        if (!averr)
        {
            _currPacketIndex = 0;
            if (_hwAccellerator && _hwAccellerator->NeedReset() == FpStateOK)
            {
				av_frame_unref(frame.get());
				avcodec_flush_buffers(_codecContext.get());
#ifdef _DEBUG
                //std::shared_ptr<AVFrame> avframeTest(av_frame_alloc(), [](AVFrame * ptr) { av_frame_free(&ptr); });
                //averr = avcodec_receive_frame(_codecContext.get(), avframeTest.get());
                //assert(averr == AVERROR(EAGAIN));
#endif
                _hwAccellerator->Reset();

				//_hwAccel.reset();
				//_hwAccellerator.reset();
				//_codecContext.reset();
				//_outputFormat.pixelFormat = AV_PIX_FMT_NONE;
				//initDecoder();
				_sessionPackets.splice(_sessionPackets.begin(), _groupedPackets);
				std::unique_lock<std::mutex> lock(_mtx);
				if(!_buffers.empty())
					_buffers.clear();

				if(!_sessionPackets.empty())
					averr = AVERROR(EAGAIN);
            }
			else
			{
                //通常是因为切换、重置编码器导致需要回退至第一个关键帧开始重新解码导致
				if(_dts != AV_NOPTS_VALUE && frame->pts <= _dts)
				{
                    ++_numFramesDiscard;
					continue;
				}
                else if(_numFramesDiscard > 0)
                {
                    printf("discard %lld frames\n", _numFramesDiscard);
                    _numFramesDiscardTotal += _numFramesDiscard;
                    _numFramesDiscard = 0;
                }
                else{}
				break;
			}
        }

        if (averr == AVERROR(EAGAIN))
        {
            ++_currPacketIndex;

            Packet packet{};
			if(_sessionPackets.empty())
				_state = _reader->NextPacket(packet, std::numeric_limits<uint32_t>::max());
			else
			{
				packet = _sessionPackets.front();
				_sessionPackets.pop_front();
			}

             assert(_state >= 0 || _state == FpStateEOF);
            if (_state < 0)
                break;

            if(!packet.ptr)
            {
                _state = FpStateEOF;
                break;
            }

            //printf("packet [%lld] pos=%lld, pts=%lld, dur=%lld.\n", packet.localIndex, packet.ptr->pos, packet.ptr->pts, packet.ptr->duration);

            averr = avcodec_send_packet(_codecContext.get(), packet.ptr.get());
            assert(averr >= 0);
            if (averr < 0)
            {
                print_averrr(averr);
                _state = FpStateBadState;
                break;
            }

			if(packet.ptr->flags & AV_PKT_FLAG_KEY)
				_groupedPackets.clear();
			_groupedPackets.push_back(packet);
        }
        else
        {
            print_averrr(averr);
            //udata.state = audio_play_state_error;
            //break;
            //读取下一个 packet
            // QQ Music 转码后，ff_flac_decode_frame_header 会出现末尾解包错误。
            //_state = FpStateBadState;
            //break;
        }
    }
    //frame->pts = frame->best_effort_timestamp;
    //frame->opaque = (void *)(av_gettime_relative() - tsStart);

    return _state;
}

void FFmpegVideoDecoder::decoderThread()
{
    thread_set_name(0, "FFmpegVideoDecoder::decoderThread");
    //thread_prom();

    initDecoder();

    if (!_reader || !_inputParam.codecId || _outputFormat.pixelFormat == AV_PIX_FMT_NONE)
    {
        _state = FpStateBadState;
        return;
    }

    //-------------------------------------------------------------
    int64_t sessionIndex = _sessionIndex - 1;
    VideoBuffer buffer = {};
    FpState error = FpStateOK;
    while (_state >= 0 && error >= 0)
    {
        std::shared_ptr<AVFrame> avframe;
        _state = decodeFrame(avframe);

        //处理最后一帧
        if (_state == FpStateEOF)
            break;

        if (_state < 0)
            break;

        _state = scaleFrame(avframe, buffer);
        if (_state < 0)
            break;

        std::unique_lock<std::mutex> lock(_mtx);
        _condDecoder.wait(lock, [this, &sessionIndex] {return _buffers.size() < _maxFrames || _state < 0 || sessionIndex != _sessionIndex || (_flags & FpFlagStop); });
        if (_flags & FpFlagStop)
            break;

        if (sessionIndex == _sessionIndex)
        {
            _buffers.emplace_back(std::move(buffer));
            lock.unlock();
            _condRead.notify_all();
        }
        else
        {
            //重建资源
            sessionIndex = _sessionIndex;
            buffer = {};
            lock.unlock();
            _sws.reset();
        }
    }
    _condRead.notify_all();

    _codecContext.reset();
}
