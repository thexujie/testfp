#include "stdafx.h"
#include "AudioPlayer.h"
#include <process.h>
#include "avobject.h"

static void audio_context_free(audio_context * ptr)
{
    if(!ptr)
        return;

    avformat_free_context(ptr->avformatContext);
    avcodec_free_context(&ptr->avcodecContext);
    swr_free(&ptr->swr);
}

static void sdl_user_data_free(sdl_user_data * ptr)
{
    if(!ptr)
        return;

    audio_context_free(&ptr->context);
    av_free(ptr->avpacket);
    for(int cnt = 0; cnt < MAX_BUFFER; ++cnt)
    {
        if(ptr->buffers[cnt].data)
        {
            av_free(ptr->buffers[cnt].data);
            ptr->buffers[cnt].data = 0;
        }
    }
    free(ptr);
}

AudioPlayer::AudioPlayer(): m_hPlayThread(NULL)
{
    SDL_memset(&m_audioSpecObtained, 0, sizeof(SDL_AudioSpec));
    if(m_hPlayThread)
    {
        ::InterlockedExchange(&m_endPlayThread, 1);
        DWORD dwWait = ::WaitForSingleObject(m_hPlayThread, 3 * 1000);
        //if(dwWait == WAIT_TIMEOUT)
        //    ::TerminateThread(m_hPlayThread, 0);
        m_hPlayThread = 0;
    }
}

AudioPlayer::~AudioPlayer()
{

}

int AudioPlayer::playAudio(const char * filename)
{
    if(!filename)
        return APErrorInvalidParameters;

    m_cs.lock();
    m_playLists.emplace_back(filename);
    m_cs.unlock();

    if(!m_hPlayThread)
        m_hPlayThread = (HANDLE)_beginthreadex(NULL, 0, playThread, (void *)this, 0, NULL);

    return APErrorNone;
}

int AudioPlayer::play(std::string filename, audio_context & context)
{
    if(!m_audioSpecObtained.freq)
        return APErrorNotReady;

    int averr = 0;
    avobject2<AVFormatContext, avformat_free_context> avformatContext(avformat_alloc_context());
    if(!avformatContext)
        return APErrorNullptr;

    averr = avformat_open_input(&avformatContext, filename.c_str(), NULL, NULL);
    if(averr)
        return APErrorGeneric;

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

    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    int out_sample_rate = m_audioSpecObtained.freq;
    //Out Buffer Size

    //FIX:Some Codec's Context Information is missing
    int64_t in_channel_layout = av_get_default_channel_layout(avcodecContext->channels);

    struct SwrContext * swr = NULL;
    if(out_sample_rate != avcodecParameters->sample_rate)
    {
#if LIBSWRESAMPLE_VERSION_MAJOR >= 3
        swr = swr_alloc();
        av_opt_set_int(swr, "ich", avcodecContext->channels, 0);
        av_opt_set_int(swr, "och", out_channels, 0);

        av_opt_set_int(swr, "in_channel_layout", in_channel_layout, 0);
        av_opt_set_int(swr, "in_sample_rate", avcodecContext->sample_rate, 0);
        av_opt_set_sample_fmt(swr, "in_sample_fmt", avcodecContext->sample_fmt, 0);

        av_opt_set_int(swr, "out_channel_layout", out_channel_layout, 0);
        av_opt_set_int(swr, "out_sample_rate", out_sample_rate, 0);
        av_opt_set_sample_fmt(swr, "out_sample_fmt", out_sample_fmt, 0);
#else
        swr = swr_alloc_set_opts(NULL, out_channel_layout, out_sample_fmt, out_sample_rate,
            in_channel_layout, avcodecContext->sample_fmt, avcodecContext->sample_rate, 0, NULL);
#endif
        swr_init(swr);
    }

    int out_nb_samples_preffer = 0;
    switch(avcodecParameters->codec_id)
    {
    case AV_CODEC_ID_ADPCM_MS:
        out_nb_samples_preffer = 2 + 2 * (avcodecParameters->block_align - 7 * avcodecParameters->channels) / avcodecParameters->channels;
        break;
    case AV_CODEC_ID_PCM_S16LE:
        //out_nb_samples = 1024;
        break;
    case AV_CODEC_ID_MP3:
        out_nb_samples_preffer = av_rescale_rnd(avcodecParameters->frame_size, out_sample_rate, avcodecParameters->sample_rate, AV_ROUND_UP);
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
    context.out_sample_fmt = out_sample_fmt;
    context.out_sample_rate = out_sample_rate;
    context.out_channels = out_channels;
    context.out_nb_samples_preffer = out_nb_samples_preffer;

    return APErrorNone;
}

int AudioPlayer::doPlay()
{
    initSDL();

    int averr = 0;

    avobject3<AVFrame, av_frame_free> avframe(av_frame_alloc());

    std::vector<std::string> playLists;
    int index = 0;
    while(!m_endPlayThread)
    {
        playLists.clear();
        //一次只播放 1 个
        if(m_cs.tryLock())
        {
            if(!m_playLists.empty())
            {
                playLists.swap(m_playLists);
            }
            m_cs.unlock();
        }

        //加入播放列表
        for(size_t cnt = 0; cnt < playLists.size(); ++cnt)
        {
            audio_context context = {};
            int err = play(playLists[cnt], context);
            if(err)
                continue;

            sdl_user_data udata = {};
            udata.context = context;
            sdl_datas.push_back(udata);
        }

        if(sdl_datas.empty())
        {
            if(SDL_GetAudioDeviceStatus(m_audioDid) == SDL_AUDIO_PLAYING)
                SDL_PauseAudioDevice(m_audioDid, SDL_TRUE);
            SDL_Delay(100);
            continue;
        }
        else
        {
            if(SDL_GetAudioDeviceStatus(m_audioDid) != SDL_AUDIO_PLAYING)
                SDL_PauseAudioDevice(m_audioDid, SDL_FALSE);
        }

        int index = 0;
        for(size_t cnt = 0; cnt < sdl_datas.size(); ++cnt)
        {
            sdl_user_data & udata = sdl_datas[cnt];
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
                            udata.state = audio_play_state_end;
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

                        printf("index:%5d\t pts:%lld\t packet size:%d\n", udata.packet_index, udata.avpacket->pts, udata.avpacket->size);
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
                        int max_frame_size = udata.context.out_sample_rate * 4;
                        buffer.data = (uint8_t *)av_malloc(max_frame_size * udata.context.out_channels);
                        buffer.cap = max_frame_size;
                    }

                    int out_buffer_size = 0;
                    ////无需重采样
                    //if(udata.context.avcodecParameters->sample_rate == udata.context.out_sample_rate)
                    //{
                    //    out_buffer_size = avframe->linesize[0];
                    //    memcpy(buffer.data, avframe->data[0], out_buffer_size);
                    //}
                    //else if(udata.context.swr)
                    {
                        averr = swr_convert(udata.context.swr, &buffer.data, buffer.cap, (const uint8_t **)avframe->data, avframe->nb_samples);
                        if(averr < 0)
                        {
                            udata.state = audio_play_state_error;
                            //printf("Error in swr_convert audio frame.\n");
                            break;
                        }
                        out_buffer_size = av_samples_get_buffer_size(NULL, udata.context.out_channels, averr, udata.context.out_sample_fmt, 1);
                    }
                    buffer.len = out_buffer_size;
                    buffer.pos = 0;
                    InterlockedAdd(&udata.decodeIndex, 1);

                    break;
                }
            }

            SDL_Delay(1);
        }

        //处理结束信息
        //SDL_Event event;
        //while(SDL_PollEvent(&event) > 0)
        //{
        //    //if(event.type == SDL_QUIT)
        //    //{
        //    //}
        //    if(event.type == SDL_AUDIODEVICEREMOVED)
        //    {
        //        SDL_AudioDeviceID adid = event.adevice.which;
        //        std::vector<sdl_user_data *>::iterator iter = sdl_datas.begin();
        //        while(iter != sdl_datas.end())
        //        {
        //            sdl_user_data * pudata = *iter;
        //            if(!pudata)
        //            {
        //                iter = sdl_datas.erase(iter);
        //                continue;
        //            }

        //            ++iter;
        //        }
        //    }
        //}

        //清理出错的、已结束的
        std::vector<sdl_user_data>::iterator iter = sdl_datas.begin();
        while(iter != sdl_datas.end())
        {
            sdl_user_data & udata = *iter;
            if(udata.state != audio_play_state_ok)
            {
                SDL_Delay(udata.avpacket->duration / 1000);
                sdl_user_data_free(&udata);
                iter = sdl_datas.erase(iter);
                continue;
            }
            ++iter;
        }

        SDL_Delay(0);
    }

    SDL_CloseAudioDevice(m_audioDid);
    return APErrorNone;
}

void AudioPlayer::doMix()
{
    
}

int AudioPlayer::initSDL()
{
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);

    //Init
    if(SDL_Init(SDL_INIT_AUDIO))
    {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    //SDL_AudioSpec
    SDL_AudioSpec desired_spec = {};
    desired_spec.freq = 44100;
    desired_spec.format = AUDIO_S16LSB;
    desired_spec.channels = out_channels;
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

    m_audioDid = SDL_OpenAudioDevice(NULL, 0, &desired_spec, &m_audioSpecObtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if(!m_audioDid)
    {
        printf("can't open audio.\n");
        return -1;
    }
    return 0;
}
    
unsigned AudioPlayer::playThread(void * args)
{
    AudioPlayer * pThis = (AudioPlayer * )args;
    pThis->doPlay();
    return 0;
}

void AudioPlayer::sdlMix(void * udata, Uint8 * stream, int len)
{
    AudioPlayer * pThis = (AudioPlayer *)udata;
    pThis->doMix();
}