#include "stdafx.h"
#include "FFmpegAudioDecoder.h"

#define DEMUXER_ASYNCHRONOUS 1
#define DECODE_ASYNCHRONOUS 1

FFmpegAudioDecoder::FFmpegAudioDecoder(std::shared_ptr<IAudioPacketReader> reader)
    : _reader(reader)
{
    _inputParam = _reader->GetAudioDecodeParam();
}

FFmpegAudioDecoder::~FFmpegAudioDecoder()
{
    if (_thDecoder.joinable())
    {
        _flags = _flags | FpFlagStop;
        _condDecoder.notify_all();
        _thDecoder.join();
    }
}

std::shared_ptr<IAudioPacketReader> FFmpegAudioDecoder::Stream() const
{
    return _reader;
}

AudioFormat FFmpegAudioDecoder::GetOutputFormat() const
{
    return _outputFormat;
}

FpState FFmpegAudioDecoder::SetOutputFormat(AudioFormat format)
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

FpState FFmpegAudioDecoder::Ready(int64_t timeoutMS)
{
    return WaitForFrames(timeoutMS);
}

FpState FFmpegAudioDecoder::WaitForFrames(int64_t timeoutMS)
{
    if (!timeoutMS)
        timeoutMS = _asyncTimeOutTime;

    std::unique_lock<std::mutex> lock(_mtx);

    if (_thDecoder.get_id() == std::thread::id())
        _thDecoder = std::thread(std::bind(&FFmpegAudioDecoder::decoderThread, this));

    if (_condRead.wait_for(lock, std::chrono::milliseconds(timeoutMS), [this] {return !_buffers.empty(); }))
        return FpStateOK;
    else
        return FpStateTimeOut;
}

FpState FFmpegAudioDecoder::PeekBuffer(AudioBuffer & buffer)
{
    if (_outputFormat.sampleFormat == AV_SAMPLE_FMT_NONE)
        return {};

    if (_state < 0 && _state != FpStateEOF)
        return _state;

    if (_thDecoder.get_id() == std::thread::id())
    {
        _thDecoder = std::thread(std::bind(&FFmpegAudioDecoder::decoderThread, this));
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

FpState FFmpegAudioDecoder::NextBuffer(AudioBuffer & buffer, int64_t timeoutMS)
{
    if (!timeoutMS)
        timeoutMS = _asyncTimeOutTime;

    if (_outputFormat.sampleFormat == AV_SAMPLE_FMT_NONE)
        return FpStateEOF;

    if (_state < 0 && _state != FpStateEOF)
        return _state;

    std::unique_lock<std::mutex> lock(_mtx);
    if (_thDecoder.get_id() == std::thread::id())
    {
        _thDecoder = std::thread(std::bind(&FFmpegAudioDecoder::decoderThread, this));
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

FpState FFmpegAudioDecoder::decodeFrame(std::shared_ptr<AVFrame> avframe, AudioBuffer & buffer)
{
    int32_t averr = 0;
    if(!_swr)
    {
        _swr.reset(swr_alloc(), [](SwrContext * ptr) { swr_free(&ptr); });
        int64_t in_channel_layout = av_get_default_channel_layout(_inputParam.format.chanels);
        int64_t out_channel_layout = av_get_default_channel_layout(_outputFormat.chanels);

        av_opt_set_int(_swr.get(), "in_channel_layout", in_channel_layout, 0);
        av_opt_set_int(_swr.get(), "in_sample_rate", _inputParam.format.sampleRate, 0);
        av_opt_set_sample_fmt(_swr.get(), "in_sample_fmt", _inputParam.format.sampleFormat, 0);

        av_opt_set_int(_swr.get(), "out_channel_layout", out_channel_layout, 0);
        av_opt_set_int(_swr.get(), "out_sample_rate", _outputFormat.sampleRate, 0);
        av_opt_set_sample_fmt(_swr.get(), "out_sample_fmt", _outputFormat.sampleFormat, 0);

        averr = swr_init(_swr.get());
        if (averr < 0)
        {
            _state = FpStateBadState;
            return FpStateBadState;
        }
    }

    uint32_t bytesPerSample = av_get_bytes_per_sample(_outputFormat.sampleFormat);
    buffer.avframe = avframe;
    buffer.numSamples = swr_get_out_samples(_swr.get(), avframe->nb_samples);
    //buffer.data = std::shared_ptr<uint8_t>(static_cast<uint8_t *>(av_malloc(buffer.numSamples * bytesPerSample)), av_free);
    //TODO buffers......
    buffer.data = std::shared_ptr<uint8_t>(new uint8_t[buffer.numSamples * bytesPerSample * _outputFormat.chanels], [](uint8_t * ptr) {delete[] ptr; });
    buffer.pts = avframe->pts == AV_NOPTS_VALUE ? std::numeric_limits<double_t>::quiet_NaN() : avframe->pts * av_q2d(_inputParam.timeBase);

    int64_t numSamples = 0;
    while (numSamples < buffer.numSamples)
    {
        uint8_t * dst = buffer.data.get() + numSamples * bytesPerSample * _outputFormat.chanels;
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

FpState FFmpegAudioDecoder::readFrame(std::shared_ptr<AVFrame> & frame)
{
    {
        std::unique_lock<std::mutex> lock(_mtx);
        if (!_sessionFrames.empty())
        {
            frame = _sessionFrames.back();
            _sessionFrames.pop_back();
            return FpStateOK;
        }
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

void FFmpegAudioDecoder::decoderThread()
{
    thread_set_name(0, "audioDecoderThread");
    //thread_prom();

    if (!_reader || !_inputParam.codecId || _outputFormat.sampleFormat == AV_SAMPLE_FMT_NONE)
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

        _codec->codec_type = AVMEDIA_TYPE_AUDIO;
        _codec->codec_id = _inputParam.codecId;
        _codec->codec_tag = _inputParam.codecTag;

        _codec->sample_fmt = _inputParam.format.sampleFormat;
        _codec->channels = _inputParam.format.chanels;
        _codec->sample_rate = _inputParam.format.sampleRate;

        _codec->bit_rate = _inputParam.bitRate;
        _codec->bits_per_coded_sample = _inputParam.bitsPerCodedSample;
        _codec->bits_per_raw_sample = _inputParam.bitsPerRawSample;
        _codec->profile = _inputParam.profile;
        _codec->level = _inputParam.level;

        _codec->block_align = _inputParam.blockAlign;
        _codec->frame_size = _inputParam.frameSize;
        _codec->delay = _codec->initial_padding = _inputParam.initialPadding;
        _codec->seek_preroll = _inputParam.seekPreRoll;

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
    FpState error = FpStateOK;
    while (_state >= 0 && error >= 0)
    {
        std::shared_ptr<AVFrame> avframe;
        _state = readFrame(avframe);
        if (_state == FpStatePending)
            break;

        if (_state != 0)
            break;

        AudioBuffer buffer{};
        _state = decodeFrame(avframe, buffer);
        if (_state < 0)
            break;

        std::unique_lock<std::mutex> lock(_mtx);
        _condDecoder.wait(lock, [this, &sessionIndex] {return _buffers.size() < _maxFrames || _state < 0 || sessionIndex != _sessionIndex || (_flags & FpFlagStop); });
        if (_flags & FpFlagStop)
            break;

        if (sessionIndex == _sessionIndex)
        {
            _buffers.emplace_back(buffer);
            buffer = {};
            lock.unlock();
            _condRead.notify_all();
        }
        else
        {
            //重建资源
            sessionIndex = _sessionIndex;
            _sessionFrames.push_front(buffer.avframe);
            buffer = {};
            lock.unlock();
            _swr.reset();
        }
    }
}
