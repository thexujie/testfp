#include "stdafx.h"
#include "AudioPlayer.h"
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
    context.stream_index = stream_index;
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

        if(SDL_GetAudioDeviceStatus(m_sdlDeviceId) != SDL_AUDIO_PLAYING)
            SDL_PauseAudioDevice(m_sdlDeviceId, SDL_FALSE);

        for(size_t cnt = 0; cnt < sdl_datas.size(); ++cnt)
        {
            audio_play_conntext & udata = sdl_datas[cnt];
            while(udata.decodeIndex - udata.playIndex < MAX_BUFFER - 1 && udata.state == audio_play_state_ok)
            {
                if(!udata.avpacket)
                    udata.avpacket = (AVPacket *)av_malloc(sizeof(AVPacket));

                //需要读取一个包
                while(udata.state == audio_play_state_ok)
                {
                    if(udata.frame_index == 0)
                    {
                        av_init_packet(udata.avpacket);
                        averr = av_read_frame(udata.context.avformatContext, udata.avpacket);
                        if(averr == AVERROR_EOF)
                        {
                            //没了
                            udata.state = audio_play_state_decode_end;
                            break;
                        }

                        if(averr)
                        {
                            udata.state = audio_play_state_error;
                            break;
                        }

                        if(udata.avpacket->dts == AV_NOPTS_VALUE)
                            continue;

                        if(udata.avpacket->stream_index != udata.context.stream_index)
                            continue;

                        //printf("index:%5d\t pts:%lld\t packet size:%d\n", udata.packet_index, udata.avpacket->pts, udata.avpacket->size);
                        ++udata.packet_index;

                        averr = avcodec_send_packet(udata.context.avcodecContext, udata.avpacket);
                        if(averr)
                        {
                            udata.state = audio_play_state_error;
                            break;
                        }
                    }

                    ++udata.frame_index;
                    averr = avcodec_receive_frame(udata.context.avcodecContext, avframe);
                    if(averr == AVERROR(EAGAIN))
                    {
                        //读取下一个 packet
                        udata.frame_index = 0;
                        continue;
                    }

                    if(averr < 0)
                    {
                        udata.state = audio_play_state_error;
                        //char buff[1024];
                        //av_strerror(averr, buff, 1024);
                        break;
                    }

                    //推送 PCM
                    audio_buffer & buffer = udata.buffers[udata.decodeIndex % MAX_BUFFER];
                    if(!buffer.data)
                    {
                        int max_frame_size = m_outSampleRate * 4;
                        buffer.index = -1;
                        buffer.data = (uint8_t *)av_malloc(max_frame_size * m_outChannels);
                        buffer.cap = max_frame_size;
                        buffer.len = 0;
                        buffer.pos = 0;
                        buffer.samples = 0;
                    }

                    //第一个 buffer
                    if(buffer.index != udata.buffer_index)
                    {
                        buffer.index = udata.buffer_index;
                        buffer.pos = 0;
                        buffer.len = 0;
                        buffer.samples = 0;
                    }

                    int unit_size = av_get_bytes_per_sample(m_outSampleFormat);
                    ////无需转码
                    if(!udata.context.swr)
                    {
                        uint8_t * out_buffer = buffer.data + buffer.len;
                        if(av_sample_fmt_is_planar(m_outSampleFormat))
                        {
                            byte * dst = out_buffer;
                            for(int isam = 0; isam < avframe->nb_samples; ++isam)
                            {
                                for(int ich = 0; ich < m_outChannels; ++ich)
                                {
                                    uint8_t * src = avframe->data[ich] + isam * unit_size;
                                    memcpy(dst, src, unit_size);
                                    dst += unit_size;
                                }
                            }
                            buffer.len += unit_size * avframe->nb_samples * m_outChannels;
                        }
                        else
                        {
                            int out_buffer_size = avframe->linesize[0];
                            memcpy(out_buffer, avframe->data[0], out_buffer_size);
                            buffer.len += out_buffer_size;
                        }
                        buffer.samples += avframe->nb_samples;
                    }
                    else
                    {
                        uint8_t * out_buffer = buffer.data + buffer.len;
                        int nb_samples = swr_convert(udata.context.swr, &out_buffer, (buffer.cap - buffer.len) / unit_size / m_outChannels, (const uint8_t**)avframe->data, avframe->nb_samples);
                        if(nb_samples < 0)
                        {
                            udata.state = audio_play_state_error;
                            //printf("Error in swr_convert audio frame.\n");
                            break;
                        }

                        int out_buffer_size = av_samples_get_buffer_size(NULL, m_outChannels, nb_samples, m_outSampleFormat, 1);
                        buffer.len += out_buffer_size;
                        buffer.samples += nb_samples;
                    }

                    //数据量不够，继续
                    if(buffer.samples >= m_outBufferSamples)
                    {
                        ++udata.buffer_index;
                        InterlockedAdd(&udata.decodeIndex, 1);
                        if(udata.presenter_start == 0)
                            udata.presenter_start = timeGetTime();
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
        long long tmNow = timeGetTime();
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
                if(tmNow - udata.presenter_start >= duration)
                {
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

    m_csPlayMix.lock();
    for(size_t cnt = 0; cnt < sdl_datas.size(); ++cnt)
    {
        audio_play_conntext & udata = sdl_datas[cnt];
        int nb_buffers = udata.decodeIndex - udata.playIndex;
        if(nb_buffers < 1)
            continue;

        audio_buffer & buffer = udata.buffers[udata.playIndex % MAX_BUFFER];
        if(buffer.len == 0)
            return;

        int size = len > buffer.len ? buffer.len : len;
        if(size < len && nb_buffers < 2)
            continue;

        SDL_MixAudioFormat(stream, buffer.data + buffer.pos, m_sdlSampleFormat, size, SDL_MIX_MAXVOLUME);
        buffer.pos += size;
        buffer.len -= size;
        if(buffer.len == 0)
            InterlockedAdd(&udata.playIndex, 1);

        int len2 = len - size;
        if(len2)
        {
            //使用第二个包的数据
            audio_buffer & buffer2 = udata.buffers[udata.playIndex % MAX_BUFFER];
            if(buffer2.len == 0)
                continue;

            int size2 = len2 > buffer2.len ? buffer2.len : len2;
            if(size2 < len2 && nb_buffers < 2)
                continue;

            SDL_MixAudioFormat(stream + size, buffer2.data + buffer2.pos, m_sdlSampleFormat, size2, SDL_MIX_MAXVOLUME);
            buffer2.pos += size2;
            buffer2.len -= size2;
            if(buffer2.len == 0)
                InterlockedAdd(&udata.playIndex, 1);
        }
    }
    m_csPlayMix.unlock();
}

unsigned AudioPlayer::playThread(void * args)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    AudioPlayer * pThis = (AudioPlayer * )args;
    pThis->doPlay();
    return 0;
}

void AudioPlayer::sdlMix(void * udata, Uint8 * stream, int len)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    AudioPlayer * pThis = (AudioPlayer *)udata;
    pThis->doMix(stream, len);
}