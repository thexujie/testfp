#include "stdafx.h"
#include "VideoPlayer.h"
#include <process.h>
#include "avobject.h"


static void audio_context_uninit(audio_context * ptr)
{
    if(!ptr)
        return;

    avformat_free_context(ptr->avformatContext);
    avcodec_free_context(&ptr->avcodecContext);
    swr_free(&ptr->swr);
}

static void sdl_user_data_uninit(player_conntext & context)
{
    audio_context_uninit(&context.audio);
    av_packet_free(&context.audioPacket);
    av_packet_free(&context.videoPacket);
    av_frame_free(&context.audioFrame);
    av_frame_free(&context.videoFrame);
    for(int cnt = 0; cnt < MAX_AUDIO_BUFFER; ++cnt)
    {
        if(context.audioBuffers[cnt].data)
        {
            av_free(context.audioBuffers[cnt].data);
            context.audioBuffers[cnt].data = 0;
        }
    }
}

VideoPlayer::VideoPlayer() : m_hPlayThread(NULL), m_endPlayThread(0), m_sdlDeviceId(0),
m_outSampleFormat(AV_SAMPLE_FMT_S16), m_outChannels(1),
m_outSampleRate(0), m_outBufferSamples(0)
{
    if(m_hPlayThread)
    {
        ::InterlockedExchange(&m_endPlayThread, 1);
        DWORD dwWait = ::WaitForSingleObject(m_hPlayThread, 3 * 1000);
        //if(dwWait == WAIT_TIMEOUT)
        //    ::TerminateThread(m_hPlayThread, 0);
        m_hPlayThread = 0;
    }
    m_outSampleFormat = AV_SAMPLE_FMT_FLT;
    m_outChannels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    m_sdlSampleFormat = AUDIO_F32LSB;
    m_outPixelFormat = AV_PIX_FMT_BGRA;
#ifdef AP_LOG
    m_logFile = _fsopen("../temp/out.txt", "wt", _SH_DENYWR);
#endif
}

VideoPlayer::~VideoPlayer()
{

}

int VideoPlayer::init()
{
    av_register_all();
    return APErrorNone;
}

int VideoPlayer::play(const char * filename)
{
    if(!filename)
        return APErrorInvalidParameters;

    m_csPlayList.lock();
    m_playList.emplace_back(filename);
    m_csPlayList.unlock();

    if(!m_hPlayThread)
        m_hPlayThread = (HANDLE)_beginthreadex(NULL, 0, playThread, (void *)this, 0, NULL);

    return APErrorNone;
}

int VideoPlayer::initSDL()
{
    //Init
    if(SDL_Init(SDL_INIT_AUDIO))
    {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    //SDL_AudioSpec
    SDL_AudioSpec desired_spec = {};
    SDL_AudioSpec obtained_spec = {};
    desired_spec.freq = 44100;
    desired_spec.format = m_sdlSampleFormat;
    desired_spec.channels = m_outChannels;
    desired_spec.silence = 0;
    desired_spec.samples = /*out_nb_samples*/4096;
    desired_spec.callback = sdlMix;
    desired_spec.userdata = (void *)this;

    //adid = 1;
    //if(SDL_OpenAudio(&desired_spec, 0) < 0)
    //{
    //    printf("can't open audio.\n");
    //    return -1;
    //}

    m_sdlDeviceId = SDL_OpenAudioDevice(NULL, 0, &desired_spec, &obtained_spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if(!m_sdlDeviceId)
    {
        printf("can't open audio.\n");
        return -1;
    }

    printf("Audio Device: %s\n", SDL_GetAudioDeviceName(0, 0));
    printf("\tsample rate: %d\n", obtained_spec.freq);
    printf("\tchannels: %d\n", obtained_spec.channels);
    printf("\tbuffer samples: %d\n", desired_spec.samples);

    m_outSampleRate = obtained_spec.freq;
    m_outBufferSamples = desired_spec.samples;
    m_outChannels = obtained_spec.channels;

    return 0;
}

int VideoPlayer::generate(std::string filename, audio_context & audioContext, video_context & videoContext)
{
    if(!m_outSampleRate)
        return APErrorNotReady;

    int averr = 0;
    avobject2<AVFormatContext, avformat_free_context> avformatContext(avformat_alloc_context());
    if(!avformatContext)
        return APErrorNullptr;

    averr = avformat_open_input(&avformatContext, filename.c_str(), NULL, NULL);
    if(averr)
        return APErrorGeneric;

    av_dump_format(avformatContext, 0, filename.c_str(), false);

    if(avformat_find_stream_info(avformatContext, NULL) < 0)
        return APErrorGeneric;

    int audio_stream_index = -1;
    for(int istream = 0; istream < (int)avformatContext->nb_streams; istream++)
    {
        if(avformatContext->streams[istream]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_stream_index = istream;
            break;
        }
    }

    if(audio_stream_index == -1)
        return APErrorGeneric;

    int video_stream_index = -1;
    for(int istream = 0; istream < (int)avformatContext->nb_streams; istream++)
    {
        if(avformatContext->streams[istream]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = istream;
            break;
        }
    }

    if(video_stream_index == -1)
        return APErrorGeneric;


    //---音频
    AVCodecParameters * avcodecAudioParameters = avformatContext->streams[audio_stream_index]->codecpar;
    AVCodec * avcodecAudio = avcodec_find_decoder(avcodecAudioParameters->codec_id);
    if(!avcodecAudio)
        return APErrorNoDecoder;

    avobject3<AVCodecContext, avcodec_free_context> avcodecAudioContext(avcodec_alloc_context3(avcodecAudio));
    avcodec_parameters_to_context(avcodecAudioContext, avcodecAudioParameters);

    if(avcodec_open2(avcodecAudioContext, avcodecAudio, NULL) < 0)
        return APErrorInternal;

    struct SwrContext * swr = NULL;
    if(m_outSampleRate != avcodecAudioContext->sample_rate ||
        m_outChannels != avcodecAudioContext->channels ||
        m_outSampleFormat != avcodecAudioContext->sample_fmt)
    {
        int64_t in_channel_layout = av_get_default_channel_layout(avcodecAudioContext->channels);
        int64_t out_channel_layout = av_get_default_channel_layout(m_outChannels);

#if LIBSWRESAMPLE_VERSION_MAJOR >= 3
        swr = swr_alloc();

        av_opt_set_int(swr, "in_channel_layout", in_channel_layout, 0);
        av_opt_set_int(swr, "in_sample_rate", avcodecAudioContext->sample_rate, 0);
        av_opt_set_sample_fmt(swr, "in_sample_fmt", avcodecAudioContext->sample_fmt, 0);

        av_opt_set_int(swr, "out_channel_layout", out_channel_layout, 0);
        av_opt_set_int(swr, "out_sample_rate", m_outSampleRate, 0);
        av_opt_set_sample_fmt(swr, "out_sample_fmt", m_outSampleFormat, 0);
#else
        swr = swr_alloc_set_opts(swr, out_channel_layout, m_outSampleFormat, m_outSampleRate,
            in_channel_layout, avcodecContext->sample_fmt, avcodecContext->sample_rate, 0, NULL);
#endif
        averr = swr_init(swr);
        if(averr < 0)
            return APErrorInternal;
    }


    //---视频
    AVCodecParameters * avcodecVideoParameters = avformatContext->streams[video_stream_index]->codecpar;
    AVCodec * avcodecVideo = avcodec_find_decoder(avcodecVideoParameters->codec_id);
    if(!avcodecAudio)
        return APErrorNoDecoder;

    avobject3<AVCodecContext, avcodec_free_context> avcodecVideoContext(avcodec_alloc_context3(avcodecVideo));
    avcodec_parameters_to_context(avcodecVideoContext, avcodecVideoParameters);

    if(avcodec_open2(avcodecVideoContext, avcodecVideo, NULL) < 0)
        return APErrorInternal;

    SwsContext * sws = sws_getContext(avcodecVideoContext->width, avcodecVideoContext->height, avcodecVideoContext->pix_fmt,
        avcodecVideoContext->width, avcodecVideoContext->height, m_outPixelFormat, SWS_POINT, NULL, NULL, NULL);

    audioContext.filename = filename;

    audioContext.avformatContext = avformatContext.detech();
    audioContext.avStream = audioContext.avformatContext->streams[audio_stream_index];
    audioContext.avcodecContext = avcodecAudioContext.detech();
    audioContext.swr = swr;
    audioContext.avcodecParameters = avcodecAudioParameters;
    audioContext.avcodec = avcodecAudio;
    audioContext.streamIndex = audio_stream_index;

    videoContext.avformatContext = audioContext.avformatContext;
    videoContext.avStream = audioContext.avformatContext->streams[video_stream_index];
    videoContext.avcodecContext = avcodecVideoContext.detech();
    videoContext.sws = sws;
    videoContext.avcodecParameters = avcodecVideoParameters;
    videoContext.avcodec = avcodecVideo;
    videoContext.streamIndex = video_stream_index;
    return APErrorNone;
}

int VideoPlayer::doPlay()
{
    initSDL();

    int averr = 0;

    while(!m_endPlayThread)
    {
        std::vector<std::string> fileList;
        if(m_csPlayList.tryLock())
        {
            if(!m_playList.empty())
            {
                fileList.swap(m_playList);
            }
            m_csPlayList.unlock();
        }

        m_csPlayMix.lock();
        //加入播放列表
        for(size_t cnt = 0; cnt < fileList.size(); ++cnt)
        {
            audio_context audioContext = {};
            video_context videoContext = {};
            int err = generate(fileList[cnt], audioContext, videoContext);
            if(err)
                continue;

            player_conntext udata = {};
            udata.audio = audioContext;
            udata.video = videoContext;
            sdl_datas.push_back(udata);

            long long dur = audioContext.avformatContext->duration;
            int min = dur / AV_TIME_BASE / 60;
            int sec = (dur / AV_TIME_BASE) % 60;
            int msec = (dur / (AV_TIME_BASE / 100)) % 100;

            printf("Audio: %s, %02d:%02d.%02d, %s(%s), %d channels, %d bits, %d Hz, %lld kbps\n",
                udata.audio.filename.c_str(),
                min, sec, msec,
                avcodec_get_name(audioContext.avcodecContext->codec_id),
                av_get_sample_fmt_name(audioContext.avcodecContext->sample_fmt),
                audioContext.avcodecContext->channels,
                av_get_bytes_per_sample(audioContext.avcodecContext->sample_fmt) * 8,
                audioContext.avcodecContext->sample_rate,
                audioContext.avcodecContext->bit_rate / 100);
        }
        m_csPlayMix.unlock();

        if(sdl_datas.empty())
        {
            if(SDL_GetAudioDeviceStatus(m_sdlDeviceId) == SDL_AUDIO_PLAYING)
                SDL_PauseAudioDevice(m_sdlDeviceId, SDL_TRUE);
            SDL_Delay(100);
            continue;
        }

#if SDL_PLAY
        if(SDL_GetAudioDeviceStatus(m_sdlDeviceId) != SDL_AUDIO_PLAYING)
            SDL_PauseAudioDevice(m_sdlDeviceId, SDL_FALSE);
#endif

        int unit_size = av_get_bytes_per_sample(m_outSampleFormat);

        for(size_t cnt = 0; cnt < sdl_datas.size(); ++cnt)
        {
            player_conntext & udata = sdl_datas[cnt];
            if(udata.dtsBase == 0)
                udata.dtsBase = timeGetTime() * TIME_BASE_MS;

            //音频
            while(udata.state == audio_play_state_ok)
            {
                bool needAudio = udata.audioDecodeIndex - udata.audioPlayIndex < MAX_AUDIO_BUFFER - 1;
                if(!needAudio)
                    break;

                //需要读取一个包
                //声音
                while(udata.audioPackets.empty() && udata.state == audio_play_state_ok)
                {
                    AVPacket * avpacket = av_packet_alloc();
                    av_init_packet(avpacket);
                    averr = av_read_frame(udata.audio.avformatContext, avpacket);
                    if(averr == AVERROR_EOF)
                    {
                        //没了
                        av_packet_unref(avpacket);
                        udata.state = audio_play_state_decode_end;
                        break;
                    }

                    if(averr)
                    {
                        log(m_logFile, "av_read_frame %d.\n", averr);
                        av_packet_unref(avpacket);
                        udata.state = audio_play_state_error;
                        break;
                    }

                    //if(udata.avpacket->dts == AV_NOPTS_VALUE)
                    //{
                    //    av_packet_unref(udata.avpacket);
                    //    continue;
                    //}

                    //视频
                    if(avpacket->stream_index == udata.video.streamIndex)
                    {
                        udata.videoPackets.push(avpacket);
                        continue;
                    }

                    if(avpacket->stream_index != udata.audio.streamIndex)
                    {
                        av_packet_unref(avpacket);
                        continue;
                    }

                    //printf("index:%5d\t pts:%lld\t packet size:%d ptsOffset:%lld\n", udata.packetIndex, udata.avpacket->pts, udata.avpacket->size, udata.ptsOffset);
                    ++udata.packetIndex;
                    udata.audioPackets.push(avpacket);
                }

                while(!udata.audioPackets.empty() && !udata.audioPacket && udata.state == audio_play_state_ok)
                {
                    udata.audioPacket = udata.audioPackets.front();
                    udata.audioPackets.pop();
                    if(!udata.audioPacket)
                    {
                        continue;
                    }

                    averr = avcodec_send_packet(udata.audio.avcodecContext, udata.audioPacket);
                    if(averr)
                    {
                        log(m_logFile, "avcodec_send_packet %d.\n", averr);
                        udata.state = audio_play_state_error;
                        av_packet_unref(udata.audioPacket);
                        continue;
                    }

                    udata.audio.sampleIndex = -1;
                }

                //帧首
                if(udata.audio.sampleIndex < 0)
                {
                    if(!udata.audioFrame)
                        udata.audioFrame = av_frame_alloc();

                    averr = avcodec_receive_frame(udata.audio.avcodecContext, udata.audioFrame);
                    if(averr == AVERROR(EAGAIN))
                    {
                        //读取下一个 packetav_free_packet(&packet);
                        av_frame_unref(udata.audioFrame);
                        av_packet_unref(udata.audioPacket);
                        udata.audioPacket = 0;
                        continue;
                    }

                    if(averr < 0)
                    {
                        log(m_logFile, "avcodec_receive_frame %d.\n", averr);
                        //udata.state = audio_play_state_error;
                        //break;
                        //读取下一个 packet
                        // QQ Music 转码后，ff_flac_decode_frame_header 会出现末尾解包错误。
                        av_frame_unref(udata.audioFrame);
                        av_packet_unref(udata.audioPacket);
                        udata.audioPacket = 0;
                        continue;
                    }
                    ++udata.audioFrameIndex;
                    udata.audio.sampleIndex = udata.audioSampleIndex;
                }

                audio_buffer & buffer = udata.audioBuffers[udata.audioDecodeIndex % MAX_AUDIO_BUFFER];
                if(!buffer.data)
                {
                    buffer.index = -1;
                    buffer.data = (uint8_t *)av_malloc(m_outBufferSamples * unit_size * m_outChannels);
                    buffer.sampleIndex = 0;
                    buffer.sampleSize = m_outBufferSamples;
                }

                //第一个 buffer
                if(buffer.index != udata.audioBufferIndex)
                {
                    buffer.index = udata.audioBufferIndex;
                    buffer.sampleIndex = 0;
                    buffer.dts = 0;
                }

                //不同步的时候，丢掉整个 frame，通常是解码慢了或者解码线程堵塞。
                // buffer.pts 不会超前

                //TODO
                long long nb_samples_have = swr_get_out_samples(udata.audio.swr, udata.audioSampleIndex == udata.audio.sampleIndex ? udata.audioFrame->nb_samples : 0);
                if(udata.dtsBase)
                {
                    long long dts = timeGetTime() * TIME_BASE_MS;
                    long long nb_samples_sync = (dts - udata.dtsBase) * m_outSampleRate / TIME_BASE_S - udata.audioSampleIndex + (udata.audioDecodeIndex - udata.audioPlayIndex) * m_outBufferSamples;
                    if(nb_samples_sync > nb_samples_have)
                    {
                        log(m_logFile, "skip frame %lld samples, %lld-%lld-%lld\n", nb_samples_have, udata.packetIndex, udata.audioFrameIndex, udata.audioSampleIndex);
                        udata.audio.sampleIndex = -1;
                        udata.audioSampleIndex += nb_samples_have;
                        swr_drop_output(udata.audio.swr, nb_samples_have);
                        av_frame_unref(udata.audioFrame);
                        continue;
                    }
                }

                long long nb_samples_need = buffer.sampleSize - buffer.sampleIndex;
                long long nb_samples = nb_samples_have > nb_samples_need ? nb_samples_need : nb_samples_have;

                ////无需转码
                if(!udata.audio.swr)
                {
                    if(av_sample_fmt_is_planar(m_outSampleFormat))
                    {
                        for(long long isam = 0; isam < nb_samples; ++isam)
                        {
                            for(int ich = 0; ich < m_outChannels; ++ich)
                            {
                                uint8_t * dst = buffer.data + buffer.sampleIndex * unit_size;
                                uint8_t * src = udata.audioFrame->data[ich] + (udata.audioSampleIndex - udata.audio.sampleIndex) * unit_size;
                                memcpy(dst, src, unit_size);
                            }
                            ++udata.audioSampleIndex;
                            ++buffer.sampleIndex;
                        }
                    }
                    else
                    {
                        uint8_t * dst = buffer.data + buffer.sampleIndex * unit_size * m_outChannels;
                        uint8_t * src = udata.audioFrame->data[0] + (udata.audioSampleIndex - udata.audio.sampleIndex) * unit_size * m_outChannels;

                        memcpy(dst, src, nb_samples * unit_size * m_outChannels);
                        udata.audioSampleIndex += nb_samples;
                        buffer.sampleIndex += nb_samples;
                    }
                }
                else
                {
                    uint8_t * dst = buffer.data + buffer.sampleIndex * unit_size * m_outChannels;
                    if(udata.audioSampleIndex == udata.audio.sampleIndex)
                    {
                        //swr_init(udata.context.swr);
                        averr = swr_convert(udata.audio.swr, &dst, nb_samples, (const uint8_t**)udata.audioFrame->data, udata.audioFrame->nb_samples);
                        if(averr < 0)
                        {
                            //char err[512];
                            //av_strerror(averr, err, 512);
                            udata.state = audio_play_state_error;
                            log(m_logFile, "Error in swr_convert audio frame.\n");
                            break;
                        }
                        udata.audioSampleIndex += averr;
                        buffer.sampleIndex += averr;
                    }
                    else
                    {
                        //!!! SwrContext 中如果有缓存没有读取完，会导致严重的内存泄漏。
                        averr = swr_convert(udata.audio.swr, &dst, nb_samples, 0, 0);
                        if(averr == 0)
                            break;

                        if(averr < 0)
                        {
                            log(m_logFile, "swr_convert2 %d.\n", averr);
                            udata.state = audio_play_state_error;
                            //printf("Error in swr_convert audio frame.\n");
                            break;
                        }
                        udata.audioSampleIndex += averr;
                        buffer.sampleIndex += averr;
                    }
                }

                //读取下一帧
                if((udata.audioSampleIndex - udata.audio.sampleIndex) >= udata.audioFrame->nb_samples)
                {
                    udata.audio.sampleIndex = -1;
                    av_frame_unref(udata.audioFrame);
                }

                if(buffer.sampleIndex >= buffer.sampleSize)
                {
                    buffer.dts = udata.audioSampleIndex * AV_TIME_BASE / m_outSampleRate;
                    log(m_logFile, "[%02lld:%02lld:%02lld.%03lld][%lld-%lld]: -------   %lld %lld   %3.5f\n",
                        buffer.dts / AV_TIME_BASE / 3600, (buffer.dts / AV_TIME_BASE / 60) % 60, (buffer.dts / AV_TIME_BASE) % 60, (buffer.dts / (AV_TIME_BASE / 1000)) % 1000,
                        udata.packetIndex, udata.audioSampleIndex,
                        udata.dtsBase, buffer.dts, udata.ptsAudioOffset / (double)TIME_BASE_S);
#ifdef  AP_LOG
                    fflush(m_logFile);
#endif

                    ++udata.audioBufferIndex;
#if SDL_PLAY
                    InterlockedExchange64(&udata.dtsAudio, buffer.dts);
                    InterlockedAdd64(&udata.audioDecodeIndex, 1);
                    //printf("\r[%lld] %02lld:%02lld:%02lld.%03lld  [%lld] %02lld:%02lld:%02lld.%03lld %lf",
                    //    udata.audioPlayIndex, udata.ptsAudio / AV_TIME_BASE / 3600, (udata.ptsAudio / AV_TIME_BASE / 60) % 60, (udata.ptsAudio / AV_TIME_BASE) % 60, (udata.ptsAudio / (AV_TIME_BASE / 1000)) % 1000,
                    //    udata.audioDecodeIndex, udata.dtsAudio / AV_TIME_BASE / 3600, (udata.dtsAudio / AV_TIME_BASE / 60) % 60, (udata.dtsAudio / AV_TIME_BASE) % 60, (udata.dtsAudio / (AV_TIME_BASE / 1000)) % 1000, udata.ptsAudioOffset / (double)AV_TIME_BASE);
#endif
                }
            }

            //视频
            while(udata.state == audio_play_state_ok)
            {
                bool needVideo = udata.videoDecodeIndex - udata.videoPlayIndex < MAX_VIDEO_BUFFER - 1;
                if(!needVideo)
                    break;

                //需要读取一个包
                //声音
                while(udata.videoPackets.empty() && udata.state == audio_play_state_ok)
                {
                    AVPacket * avpacket = av_packet_alloc();
                    av_init_packet(avpacket);
                    averr = av_read_frame(udata.video.avformatContext, avpacket);
                    if(averr == AVERROR_EOF)
                    {
                        //没了
                        av_packet_unref(avpacket);
                        udata.state = audio_play_state_decode_end;
                        break;
                    }

                    if(averr)
                    {
                        log(m_logFile, "av_read_frame %d.\n", averr);
                        av_packet_unref(avpacket);
                        udata.state = audio_play_state_error;
                        break;
                    }

                    //if(udata.avpacket->dts == AV_NOPTS_VALUE)
                    //{
                    //    av_packet_unref(udata.avpacket);
                    //    continue;
                    //}

                    //视频
                    if(avpacket->stream_index == udata.audio.streamIndex)
                    {
                        udata.audioPackets.push(avpacket);
                        continue;
                    }

                    if(avpacket->stream_index != udata.video.streamIndex)
                    {
                        av_packet_unref(avpacket);
                        continue;
                    }

                    //printf("index:%5d\t pts:%lld\t packet size:%d ptsOffset:%lld\n", udata.packetIndex, udata.avpacket->pts, udata.avpacket->size, udata.ptsOffset);
                    ++udata.packetIndex;
                    udata.videoPackets.push(avpacket);
                }

                while(!udata.videoPackets.empty() && !udata.videoPacket && udata.state == audio_play_state_ok)
                {
                    udata.videoPacket = udata.videoPackets.front();
                    udata.videoPackets.pop();
                    if(!udata.videoPacket)
                    {
                        continue;
                    }

                    averr = avcodec_send_packet(udata.video.avcodecContext, udata.videoPacket);
                    if(averr)
                    {
                        log(m_logFile, "avcodec_send_packet %d.\n", averr);
                        udata.state = audio_play_state_error;
                        av_packet_unref(udata.videoPacket);
                        continue;
                    }
                }

                if(!udata.videoFrame)
                    udata.videoFrame = av_frame_alloc();

                averr = avcodec_receive_frame(udata.video.avcodecContext, udata.videoFrame);
                ////if(averr >= 0)
                ////{
                ////    if(decoder_reorder_pts == -1)
                ////    {
                ////        frame->pts = frame->best_effort_timestamp;
                ////    }
                ////    else if(!decoder_reorder_pts)
                ////    {
                ////        frame->pts = frame->pkt_dts;
                ////    }
                ////}

                if(averr == AVERROR(EAGAIN))
                {
                    //读取下一个 packetav_free_packet(&packet);
                    av_frame_unref(udata.videoFrame);
                    av_packet_unref(udata.videoPacket);
                    udata.videoPacket = 0;
                    continue;
                }
                printf("frame %d\n", udata.video.avcodecContext->frame_number);
                if(averr < 0)
                {
                    log(m_logFile, "avcodec_receive_frame %d.\n", averr);
                    //udata.state = audio_play_state_error;
                    //break;
                    //读取下一个 packet
                    // QQ Music 转码后，ff_flac_decode_frame_header 会出现末尾解包错误。
                    av_frame_unref(udata.videoFrame);
                    av_packet_unref(udata.videoPacket);
                    udata.videoPacket = 0;
                    continue;
                }
                ++udata.videoFrameIndex;

                video_buffer & buffer = udata.videoBuffers[udata.videoDecodeIndex % MAX_VIDEO_BUFFER];
                if(!buffer.data)
                {
                    buffer.index = -1;
                    buffer.pitch = av_image_get_linesize(m_outPixelFormat, udata.video.avcodecContext->width, 0);
                    buffer.data = (uint8_t *)av_malloc(buffer.pitch * udata.video.avcodecContext->height);
                    buffer.width = udata.video.avcodecContext->width;
                    buffer.height = udata.video.avcodecContext->height;
                }

                buffer.index = udata.videoBufferIndex;
                buffer.dts = udata.videoFrame->best_effort_timestamp * TIME_BASE_S * av_q2d(udata.video.avStream->time_base);
                buffer.duration = TIME_BASE_S / 24;

                //不同步的时候，丢掉整个 frame，通常是解码慢了或者解码线程堵塞。
                // buffer.pts 不会超前

                //TODO
                //long long nb_samples_have = swr_get_out_samples(udata.audio.swr, udata.sampleIndex == udata.audio.sampleIndex ? udata.audioFrame->nb_samples : 0);
                //if(udata.dtsBase)
                //{
                //    long long dts = timeGetTime() * TIME_BASE_MS;
                //    long long nb_samples_sync = (dts - udata.dtsBase) * m_outSampleRate / TIME_BASE_S - udata.sampleIndex + (udata.audioDecodeIndex - udata.audioPlayIndex) * m_outBufferSamples;
                //    if(nb_samples_sync > nb_samples_have)
                //    {
                //        log(m_logFile, "skip frame %lld samples, %lld-%lld-%lld\n", nb_samples_have, udata.packetIndex, udata.audioFrameIndex, udata.sampleIndex);
                //        udata.audio.sampleIndex = -1;
                //        udata.sampleIndex += nb_samples_have;
                //        swr_drop_output(udata.audio.swr, nb_samples_have);
                //        av_frame_unref(udata.audioFrame);
                //        continue;
                //    }
                //}
       /*         AVFrame* frame2 = av_frame_alloc();
                av_image_fill_arrays(frame2->data, frame2->linesize, buffer.data, m_outPixelFormat, buffer.width, buffer.height, 4);*/

                uint8_t * dst[AV_NUM_DATA_POINTERS] = { buffer.data };
                int linesize[AV_NUM_DATA_POINTERS] = { buffer.pitch };
                int height = sws_scale(udata.video.sws, (const unsigned char* const*)udata.videoFrame->data, udata.videoFrame->linesize, 0, udata.videoFrame->height,
                    dst, linesize);
                av_frame_unref(udata.videoFrame);

                //buffer.dts = udata.videoSampleIndex * AV_TIME_BASE / udata.videoPacket->du;
                //log(m_logFile, "[%02lld:%02lld:%02lld.%03lld][%lld-%lld]: -------   %lld %lld   %3.5f\n",
                //    buffer.dts / AV_TIME_BASE / 3600, (buffer.dts / AV_TIME_BASE / 60) % 60, (buffer.dts / AV_TIME_BASE) % 60, (buffer.dts / (AV_TIME_BASE / 1000)) % 1000,
                //    udata.packetIndex, udata.audioSampleIndex,
                //    udata.dtsBase, buffer.dts, udata.ptsAudioOffset / (double)TIME_BASE_S);
#ifdef  AP_LOG
                    //fflush(m_logFile);
#endif

                ++udata.videoBufferIndex;
#if SDL_PLAY
                InterlockedExchange64(&udata.dtsVideo, buffer.dts);
                InterlockedAdd64(&udata.videoDecodeIndex, 1);
                //printf("\r[%lld] %02lld:%02lld:%02lld.%03lld  [%lld] %02lld:%02lld:%02lld.%03lld %lf",
                //    udata.audioPlayIndex, udata.ptsAudio / AV_TIME_BASE / 3600, (udata.ptsAudio / AV_TIME_BASE / 60) % 60, (udata.ptsAudio / AV_TIME_BASE) % 60, (udata.ptsAudio / (AV_TIME_BASE / 1000)) % 1000,
                //    udata.audioDecodeIndex, udata.dtsAudio / AV_TIME_BASE / 3600, (udata.dtsAudio / AV_TIME_BASE / 60) % 60, (udata.dtsAudio / AV_TIME_BASE) % 60, (udata.dtsAudio / (AV_TIME_BASE / 1000)) % 1000, udata.ptsAudioOffset / (double)AV_TIME_BASE);
#endif
            }
        }


        //设备关闭
        SDL_Event event;
        while(SDL_PollEvent(&event) > 0)
        {
            if(event.type == SDL_AUDIODEVICEREMOVED || event.type == SDL_QUIT)
            {
                SDL_AudioDeviceID adid = event.adevice.which;
                if(adid == m_sdlDeviceId)
                {
                    m_csPlayMix.lock();
                    std::vector<player_conntext>::iterator iter = sdl_datas.begin();
                    while(iter != sdl_datas.end())
                    {
                        player_conntext & udata = *iter;
                        sdl_user_data_uninit(udata);
                        iter = sdl_datas.erase(iter);
                    }
                    m_csPlayMix.unlock();
                }
            }
        }

        //清理出错的、已结束的
        long long tmNow = timeGetTime() * TIME_BASE_MS;
        m_csPlayMix.lock();
        std::vector<player_conntext>::iterator iter = sdl_datas.begin();
        while(iter != sdl_datas.end())
        {
            player_conntext & udata = *iter;
            //出错
            if(udata.state == audio_play_state_error)
            {
                sdl_user_data_uninit(udata);
                iter = sdl_datas.erase(iter);
                continue;
            }
            //解码完毕
            else if(udata.state == audio_play_state_decode_end)
            {
                long long duration = udata.audio.avformatContext->duration / (AV_TIME_BASE / 1000);
#if SDL_PLAY
                if(tmNow - udata.dtsBase >= duration)
#endif
                {
                    log(m_logFile, "packets: %lld, samples: %lld\n", udata.packetIndex, udata.audioSampleIndex);
#ifdef  AP_LOG
                    fflush(m_logFile);
#endif
                    sdl_user_data_uninit(udata);
                    iter = sdl_datas.erase(iter);
                    continue;
                }
            }
            else
            {

            }
            ++iter;
        }
        m_csPlayMix.unlock();
        SDL_Delay(1);
    }

    SDL_CloseAudioDevice(m_sdlDeviceId);
    return APErrorNone;
}

void VideoPlayer::doMix(Uint8 * stream, int len)
{
    SDL_memset(stream, 0, len);

    int bytePerSample = av_get_bytes_per_sample(m_outSampleFormat);
    int sampleSize = len / bytePerSample / m_outChannels;
    long long pts = timeGetTime() * TIME_BASE_MS;
    m_csPlayMix.lock();
    for(size_t cnt = 0; cnt < sdl_datas.size(); ++cnt)
    {
        player_conntext & udata = sdl_datas[cnt];
        int nb_buffers = udata.audioDecodeIndex - udata.audioPlayIndex;
        if(nb_buffers < 1)
            continue;
        audio_buffer & buffer = udata.audioBuffers[udata.audioPlayIndex % MAX_AUDIO_BUFFER];
        if(buffer.sampleSize != sampleSize)
            continue;

        SDL_MixAudioFormat(stream, buffer.data, m_sdlSampleFormat, len, SDL_MIX_MAXVOLUME);

        InterlockedExchange64(&udata.ptsAudio, buffer.dts);
        InterlockedExchange64(&udata.ptsAudioOffset, pts - udata.dtsBase - buffer.dts);
        InterlockedAdd64(&udata.audioPlayIndex, 1);
    }
    m_csPlayMix.unlock();
}

int VideoPlayer::doCombine(char * data, int width, int height, int pitch, int & duration)
{
    duration = 0;
    long long pts = timeGetTime() * TIME_BASE_MS;
    m_csPlayMix.lock();
    for(size_t cnt = 0; cnt < sdl_datas.size(); ++cnt)
    {
        player_conntext & udata = sdl_datas[cnt];
        int nb_buffers = udata.videoDecodeIndex - udata.videoPlayIndex;
        if(nb_buffers < 1)
            continue;
        video_buffer & buffer = udata.videoBuffers[udata.videoPlayIndex % MAX_VIDEO_BUFFER];

        if(width != buffer.width || height != buffer.height)
            continue;

        for(int cnt = 0; cnt < height; ++cnt)
        {
            memcpy(data + pitch * cnt, buffer.data + buffer.pitch * cnt, pitch);
        }
        duration = buffer.duration;

        InterlockedExchange64(&udata.ptsVideo, buffer.dts);
        InterlockedExchange64(&udata.ptsVideoOffset, pts - udata.dtsBase - buffer.dts);
        InterlockedAdd64(&udata.videoPlayIndex, 1);
    }
    m_csPlayMix.unlock();
    return 0;
}

unsigned VideoPlayer::playThread(void * args)
{
    //SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    VideoPlayer * pThis = (VideoPlayer *)args;
    pThis->doPlay();
    return 0;
}

void VideoPlayer::sdlMix(void * udata, Uint8 * stream, int len)
{
    //SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    VideoPlayer * pThis = (VideoPlayer *)udata;
    pThis->doMix(stream, len);
}