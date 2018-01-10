#include "stdafx.h"
#include "AudioPlayer.h"
#include <process.h>
#include "avobject.h"

const int TIME_BASE_S = AV_TIME_BASE;
const int TIME_BASE_MS = AV_TIME_BASE / 1000;

#define SDL_PLAY 1
FILE * flog = 0;
static void audio_context_uninit(audio_context * ptr)
{
    if(!ptr)
        return;

    avformat_free_context(ptr->avformatContext);
    avcodec_free_context(&ptr->avcodecContext);
    swr_free(&ptr->swr);
}

static void sdl_user_data_uninit(audio_play_conntext & context)
{
    audio_context_uninit(&context.context);
    av_free(context.avpacket);
    for(int cnt = 0; cnt < MAX_BUFFER; ++cnt)
    {
        if(context.buffers[cnt].data)
        {
            av_free(context.buffers[cnt].data);
			context.buffers[cnt].data = 0;
        }
    }
}

AudioPlayer::AudioPlayer(): m_hPlayThread(NULL), m_endPlayThread(0), m_sdlDeviceId(0),
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
    m_outSampleFormat = AV_SAMPLE_FMT_S16;
    m_outChannels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    m_sdlSampleFormat = AUDIO_S16LSB;
    flog = _fsopen("out.txt", "wt", _SH_DENYWR);
}

AudioPlayer::~AudioPlayer()
{

}

int AudioPlayer::init()
{
    av_register_all();
    return APErrorNone;
}

int AudioPlayer::playAudio(const char * filename)
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

int AudioPlayer::initSDL()
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
    desired_spec.format = AUDIO_S16LSB;
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

int AudioPlayer::generate(std::string filename, audio_context & context)
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

    //av_dump_format(avformatContext, 0, filename.c_str(), false);

    if(avformat_find_stream_info(avformatContext, NULL) < 0)
        return APErrorGeneric;

    int stream_index = -1;
    for(int istream = 0; istream < (int)avformatContext->nb_streams; istream++)
    {
        if(avformatContext->streams[istream]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            stream_index = istream;
            break;
        }
    }

    if(stream_index == -1)
        return APErrorGeneric;

    AVCodecParameters * avcodecParameters = avformatContext->streams[stream_index]->codecpar;
    AVCodec * avcodec = avcodec_find_decoder(avcodecParameters->codec_id);
    if(!avcodec)
        return APErrorNoDecoder;

    avobject3<AVCodecContext, avcodec_free_context> avcodecContext(avcodec_alloc_context3(avcodec));
    avcodec_parameters_to_context(avcodecContext, avcodecParameters);
    avcodecContext->codec_type = avcodecParameters->codec_type;
    avcodecContext->codec_id = avcodecParameters->codec_id;
    avcodecContext->bit_rate = avcodecParameters->bit_rate;
    avcodecContext->sample_rate = avcodecParameters->sample_rate;
    avcodecContext->channels = avcodecParameters->channels;
    avcodecContext->sample_fmt = (AVSampleFormat)avcodecParameters->format;

    if(avcodec_open2(avcodecContext, avcodec, NULL) < 0)
        return APErrorInternal;

    struct SwrContext * swr = NULL;
    if(m_outSampleRate != avcodecContext->sample_rate ||
        m_outChannels != avcodecContext->channels ||
        m_outSampleFormat != avcodecContext->sample_fmt)
    {
        int64_t in_channel_layout = av_get_default_channel_layout(avcodecContext->channels);
        int64_t out_channel_layout = av_get_default_channel_layout(m_outChannels);

#if LIBSWRESAMPLE_VERSION_MAJOR >= 3
        swr = swr_alloc();

        av_opt_set_int(swr, "in_channel_layout", in_channel_layout, 0);
        av_opt_set_int(swr, "in_sample_rate", avcodecContext->sample_rate, 0);
        av_opt_set_sample_fmt(swr, "in_sample_fmt", avcodecContext->sample_fmt, 0);

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

    int out_nb_samples_preffer = 0;
    switch(avcodecContext->codec_id)
    {
    case AV_CODEC_ID_ADPCM_MS:
        out_nb_samples_preffer = 2 + 2 * (avcodecParameters->block_align - 7 * avcodecParameters->channels) / avcodecParameters->channels;
        break;
    case AV_CODEC_ID_PCM_S16LE:
        //out_nb_samples = 1024;
        break;
    case AV_CODEC_ID_MP3:
        out_nb_samples_preffer = av_rescale_rnd(avcodecParameters->frame_size, m_outSampleRate, avcodecParameters->sample_rate, AV_ROUND_UP);
        break;
    default:
        break;
    }

    context.filename = filename;

    context.avformatContext = avformatContext.detech();
    context.avcodecContext = avcodecContext.detech();
    context.swr = swr;

    context.avcodecParameters = avcodecParameters;
    context.avcodec = avcodec;
    context.audioStreamIndex = stream_index;
    context.out_nb_samples_preffer = out_nb_samples_preffer;

    return APErrorNone;
}

int AudioPlayer::doPlay()
{
    initSDL();

    int averr = 0;

    avobject3<AVFrame, av_frame_free> avframe(av_frame_alloc());

    int index = 0;
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
            audio_context context = {};
            int err = generate(fileList[cnt], context);
            if(err)
                continue;

            audio_play_conntext udata = {};
            udata.context = context;
            sdl_datas.push_back(udata);

            long long dur = context.avformatContext->duration;
            int min = dur / AV_TIME_BASE / 60;
            int sec = (dur / AV_TIME_BASE) % 60;
            int msec = (dur / (AV_TIME_BASE / 100)) % 100;

            printf("Audio: %s, %02d:%02d.%02d, %s(%s), %d channels, %d bits, %d Hz, %lld kbps\n", 
                udata.context.filename.c_str(), 
                min, sec, msec,
                avcodec_get_name(context.avcodecContext->codec_id),
                av_get_sample_fmt_name(context.avcodecContext->sample_fmt),
                context.avcodecContext->channels,
                av_get_bytes_per_sample(context.avcodecContext->sample_fmt) * 8, 
                context.avcodecContext->sample_rate,
                context.avcodecContext->bit_rate / 100);
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
            audio_play_conntext & udata = sdl_datas[cnt];
            if(udata.ptsBase == 0)
                udata.ptsBase = timeGetTime() * TIME_BASE_MS;
            while(udata.decodeIndex - udata.playIndex < MAX_BUFFER - 1 && udata.state == audio_play_state_ok)
            {
                if(!udata.avpacket)
                {
                    udata.avpacket = av_packet_alloc();
                    av_init_packet(udata.avpacket);
                }

                //需要读取一个包
                while(udata.state == audio_play_state_ok)
                {
                    if(udata.avpacket->duration <= 0)
                    {
                        averr = av_read_frame(udata.context.avformatContext, udata.avpacket);
                        if(averr == AVERROR_EOF)
                        {
                            //没了
                            av_packet_unref(udata.avpacket);
                            udata.state = audio_play_state_decode_end;
                            break;
                        }

                        if(averr)
                        {
                            fprintf(flog, "av_read_frame %d.\n", averr);
                            av_packet_unref(udata.avpacket);
                            udata.state = audio_play_state_error;
                            break;
                        }

                        //if(udata.avpacket->dts == AV_NOPTS_VALUE)
                        //{
                        //    av_packet_unref(udata.avpacket);
                        //    continue;
                        //}

                        if(udata.avpacket->stream_index != udata.context.audioStreamIndex)
                        {
                            av_packet_unref(udata.avpacket);
                            continue;
                        }

                        //printf("index:%5d\t pts:%lld\t packet size:%d ptsOffset:%lld\n", udata.packetIndex, udata.avpacket->pts, udata.avpacket->size, udata.ptsOffset);
                        ++udata.packetIndex;

                        averr = avcodec_send_packet(udata.context.avcodecContext, udata.avpacket);
                        if(averr)
                        {
                            fprintf(flog, "avcodec_send_packet %d.\n", averr);
                            udata.state = audio_play_state_error;
                            av_packet_unref(udata.avpacket);
                            break;
                        }

                        udata.context.sampleIndex = -1;
                    }

                    //帧首
                    if(udata.context.sampleIndex < 0)
                    {
                        averr = avcodec_receive_frame(udata.context.avcodecContext, avframe);
                        if(averr == AVERROR(EAGAIN))
                        {
                            //读取下一个 packetav_free_packet(&packet);
                            av_frame_unref(avframe);
                            av_packet_unref(udata.avpacket);
                            continue;
                        }


                        if(averr < 0)
                        {
                            fprintf(flog, "avcodec_receive_frame %d.\n", averr);
                            //udata.state = audio_play_state_error;
                            //break;
                            //读取下一个 packet
                            // QQ Music 转码后，ff_flac_decode_frame_header 会出现末尾解包错误。
                            av_frame_unref(avframe);
                            av_packet_unref(udata.avpacket);
                            continue;
                        }
                        ++udata.frameIndex;
                        udata.context.sampleIndex = udata.sampleIndex;
                    }

                    audio_buffer & buffer = udata.buffers[udata.decodeIndex % MAX_BUFFER];
                    if(!buffer.data)
                    {
                        buffer.index = -1;
                        buffer.data = (uint8_t *)av_malloc(m_outBufferSamples * unit_size * m_outChannels);
                        buffer.sampleIndex = 0;
                        buffer.sampleSize = m_outBufferSamples;
                    }

                    //第一个 buffer
                    if(buffer.index != udata.bufferIndex)
                    {
                        buffer.index = udata.bufferIndex;
                        buffer.sampleIndex = 0;
                        buffer.pts = 0;
                    }

                    //不同步的时候，丢掉整个 frame，通常是解码慢了或者解码线程堵塞。
                    // buffer.pts 不会超前
                    //TODO
                    long long nb_samples_have = swr_get_out_samples(udata.context.swr, udata.sampleIndex == udata.context.sampleIndex ? avframe->nb_samples : 0);
                    if(udata.pts)
                    {
                        long long pts = udata.ptsBase + udata.sampleIndex * AV_TIME_BASE / m_outSampleRate;
                        long long nb_samples_sync = (udata.pts - pts) * m_outSampleRate / TIME_BASE_S;
                        if(nb_samples_sync > nb_samples_have)
                        {
                            fprintf(flog, "skip frame %lld samples, %lld-%lld-%lld\n", nb_samples_have, udata.packetIndex, udata.frameIndex, udata.sampleIndex);
                            udata.context.sampleIndex = -1;
                            udata.sampleIndex += nb_samples_have;
                            av_frame_unref(avframe);
                            continue;
                        }
                    }

                    long long nb_samples_need = buffer.sampleSize - buffer.sampleIndex;
                    long long nb_samples = nb_samples_have > nb_samples_need ? nb_samples_need : nb_samples_have;

                    ////无需转码
                    if(!udata.context.swr)
                    {
                        if(av_sample_fmt_is_planar(m_outSampleFormat))
                        {
                            for(long long isam = 0; isam < nb_samples; ++isam)
                            {
                                for(int ich = 0; ich < m_outChannels; ++ich)
                                {
                                    uint8_t * dst = buffer.data + buffer.sampleIndex * unit_size;
                                    uint8_t * src = avframe->data[ich] + (udata.sampleIndex - udata.context.sampleIndex) * unit_size;
                                    memcpy(dst, src, unit_size);
                                }
                                ++udata.sampleIndex;
                                ++buffer.sampleIndex;
                            }
                        }
                        else
                        {
                            uint8_t * dst = buffer.data + buffer.sampleIndex * unit_size * m_outChannels;
                            uint8_t * src = avframe->data[0] + (udata.sampleIndex - udata.context.sampleIndex) * unit_size * m_outChannels;

                            memcpy(dst, src, nb_samples * unit_size * m_outChannels);
                            udata.sampleIndex += nb_samples;
                            buffer.sampleIndex += nb_samples;
                        }
                    }
                    else
                    {
                        uint8_t * dst = buffer.data + buffer.sampleIndex * unit_size * m_outChannels;
                        if(udata.sampleIndex == udata.context.sampleIndex)
                        {
                            //swr_init(udata.context.swr);
                            averr = swr_convert(udata.context.swr, &dst, nb_samples, (const uint8_t**)avframe->data, avframe->nb_samples);
                            if(averr < 0)
                            {
                                //char err[512];
                                //av_strerror(averr, err, 512);
                                udata.state = audio_play_state_error;
                                fprintf(flog, "Error in swr_convert audio frame.\n");
                                break;
                            }
                            udata.sampleIndex += averr;
                            buffer.sampleIndex += averr;
                        }
                        else
                        {
                            //!!! SwrContext 中如果有缓存没有读取完，会导致严重的内存泄漏。
                            averr = swr_convert(udata.context.swr, &dst, nb_samples, 0, 0);
                            if(averr == 0)
                                break;

                            if(averr < 0)
                            {
                                fprintf(flog, "swr_convert2 %d.\n", averr);
                                udata.state = audio_play_state_error;
                                //printf("Error in swr_convert audio frame.\n");
                                break;
                            }
                            udata.sampleIndex += averr;
                            buffer.sampleIndex += averr;
                        }
                    }

                    //读取下一帧
                    if((udata.sampleIndex - udata.context.sampleIndex) >= avframe->nb_samples)
                    {
                        udata.context.sampleIndex = -1;
                        av_frame_unref(avframe);
                    }

                    if(buffer.sampleIndex >= buffer.sampleSize)
                    {
                        buffer.pts = udata.sampleIndex * AV_TIME_BASE / m_outSampleRate;
                        fprintf(flog, "[%02lld:%02lld:%02lld.%03lld][%lld-%lld]: -------   %lld %lld   %3.5f\n", 
                            buffer.pts / AV_TIME_BASE / 3600, (buffer.pts / AV_TIME_BASE / 60) % 60, (buffer.pts / AV_TIME_BASE) % 60, (buffer.pts / (AV_TIME_BASE / 1000)) % 1000,
                            udata.packetIndex, udata.sampleIndex,
                            udata.ptsBase, buffer.pts, udata.ptsOffset / (double)TIME_BASE_S);
                        fflush(flog);
                        ++udata.bufferIndex;
#if SDL_PLAY
                        InterlockedAdd(&udata.decodeIndex, 1);
#endif
                        break;
                    }
                }
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
                    std::vector<audio_play_conntext>::iterator iter = sdl_datas.begin();
                    while(iter != sdl_datas.end())
                    {
                        audio_play_conntext & udata = *iter;
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
        std::vector<audio_play_conntext>::iterator iter = sdl_datas.begin();
        while(iter != sdl_datas.end())
        {
            audio_play_conntext & udata = *iter;
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
                long long duration = udata.context.avformatContext->duration / (AV_TIME_BASE / 1000);
#if SDL_PLAY
                if(tmNow - udata.ptsBase >= duration)
#endif
                {
                    fprintf(flog, "packets: %lld, samples: %lld\n", udata.packetIndex, udata.sampleIndex);
                    fflush(flog);
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

void AudioPlayer::doMix(Uint8 * stream, int len)
{
    SDL_memset(stream, 0, len);

    long long ptsNow = timeGetTime() * TIME_BASE_MS;
    int bytePerSample = av_get_bytes_per_sample(m_outSampleFormat);
    int sampleSize = len / bytePerSample / m_outChannels;
    m_csPlayMix.lock();
    for(size_t cnt = 0; cnt < sdl_datas.size(); ++cnt)
    {
        audio_play_conntext & udata = sdl_datas[cnt];
        InterlockedExchange64(&udata.pts, ptsNow);
        int nb_buffers = udata.decodeIndex - udata.playIndex;
        if(nb_buffers < 1)
            continue;
        audio_buffer & buffer = udata.buffers[udata.playIndex % MAX_BUFFER];
        if(buffer.sampleSize != sampleSize)
            continue;

        SDL_MixAudioFormat(stream, buffer.data, m_sdlSampleFormat, len, SDL_MIX_MAXVOLUME);

        //计算时间差
        InterlockedExchange64(&udata.ptsOffset, udata.ptsBase + buffer.pts - ptsNow);
        InterlockedAdd(&udata.playIndex, 1);
    }
    m_csPlayMix.unlock();
}

unsigned AudioPlayer::playThread(void * args)
{
    //SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    AudioPlayer * pThis = (AudioPlayer * )args;
    pThis->doPlay();
    return 0;
}

void AudioPlayer::sdlMix(void * udata, Uint8 * stream, int len)
{
    //SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    AudioPlayer * pThis = (AudioPlayer *)udata;
    pThis->doMix(stream, len);
}