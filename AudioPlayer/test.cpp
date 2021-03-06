#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <timeapi.h>

//Output PCM
#define OUTPUT_PCM 0
//Use SDL
#define USE_SDL 1

struct buffer_t
{
    uint8_t * data;
    int len;
    int pos;
};

uint32_t adid = 0;
const int MAX_BUFFER = 4;
long decodeIndex = 0;
long playIndex = 0;
buffer_t buffers[MAX_BUFFER];

long g_indexA = 0;
long g_indexB = 0;
void  fill_audio(void *udata, Uint8 *stream, int len) {
    InterlockedAdd(&g_indexA, 1);
    SDL_memset(stream, 0, len);
    //SDL 2.0
    if(playIndex >= decodeIndex)
        return;

    buffer_t & buffer = buffers[playIndex % MAX_BUFFER];
    if(buffer.len == 0)
        return;
    int use = len > buffer.len ? buffer.len : len;

    SDL_MixAudioFormat(stream, buffer.data + buffer.pos, AUDIO_S16LSB, use, SDL_MIX_MAXVOLUME);
    buffer.pos += use;
    buffer.len -= use;

    if(use < len && decodeIndex - playIndex >0)
    {
        int next = len - use;
        InterlockedAdd(&playIndex, 1);

        buffer_t & buffer = buffers[playIndex % MAX_BUFFER];
        if(buffer.len == 0)
            return;

        SDL_MixAudioFormat(stream + use, buffer.data + buffer.pos, AUDIO_S16LSB, next, SDL_MIX_MAXVOLUME);
        buffer.pos += next;
        buffer.len -= next;
    }
    else
    {
        if(buffer.len == 0)
            InterlockedAdd(&playIndex, 1);
    }
}

int testPlay()
{
    SetConsoleOutputCP(CP_UTF8);

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
    SDL_AudioSpec obtained_spec = {};
    desired_spec.freq = 44100;
    desired_spec.format = AUDIO_S16LSB;
    desired_spec.channels = out_channels;
    desired_spec.silence = 0;
    desired_spec.samples = /*out_nb_samples*/1152 ;
    desired_spec.callback = fill_audio;
    desired_spec.userdata = 0;

    //adid = 1;
    //if(SDL_OpenAudio(&desired_spec, 0) < 0)
    //{
    //    printf("can't open audio.\n");
    //    return -1;
    //}

    adid = SDL_OpenAudioDevice(NULL, 0, &desired_spec, &obtained_spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if(!adid)
    {
        printf("can't open audio.\n");
        return -1;
    }
    int out_sample_rate = obtained_spec.freq;
    int max_frame_size = out_sample_rate * 4;



    memset(buffers, 0, sizeof(buffers));
    int index = 0;

    //char url[] = "../res/musics/hckz.ape";
    //char url[] = "../res/musics/flower.wav";
    //char url[] = "../res/musics/sample.wav";
    char url[] = "../res/musics/mlsx.mp3";

    av_register_all();
    avformat_network_init();
    AVFormatContext	* avformatContext = avformat_alloc_context();
    //Open
    if(avformat_open_input(&avformatContext, url, NULL, NULL) != 0)
    {
        printf("Couldn't open input stream.\n");
        return -1;
    }
    // Retrieve stream information
    if(avformat_find_stream_info(avformatContext, NULL) < 0)
    {
        printf("Couldn't find stream information.\n");
        return -1;
    }
    // Dump valid information onto standard error
    av_dump_format(avformatContext, 0, url, false);

    long long dur = avformatContext->duration;
    long long min = dur / AV_TIME_BASE / 60;
    long long sec = (dur / AV_TIME_BASE) % 60;
    long long msec = (dur / (AV_TIME_BASE / 100)) % 100;

    // Find the first audio stream
    int audioStream = -1;
    for(int istream = 0; istream < avformatContext->nb_streams; istream++)
    {
        if(avformatContext->streams[istream]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioStream = istream;
            break;
        }
    }

    if(audioStream == -1)
    {
        printf("Didn't find a audio stream.\n");
        return -1;
    }

    // Get a pointer to the codec context for the audio stream
    AVCodecParameters	* avcodecParameters = avformatContext->streams[audioStream]->codecpar;

    // Find the decoder for the audio stream
    AVCodec * avcodec = avcodec_find_decoder(avcodecParameters->codec_id);
    if(avcodec == NULL)
    {
        printf("Codec not found.\n");
        return -1;
    }

    AVCodecContext * avcodecContext = avcodec_alloc_context3(avcodec);
    avcodec_parameters_to_context(avcodecContext, avcodecParameters);
    avcodecContext->codec_type = avcodecParameters->codec_type;
    avcodecContext->codec_id = avcodecParameters->codec_id;
    avcodecContext->bit_rate = avcodecParameters->bit_rate;
    avcodecContext->sample_rate = avcodecParameters->sample_rate;
    avcodecContext->channels = avcodecParameters->channels;
    avcodecContext->sample_fmt = (AVSampleFormat)avcodecParameters->format;

    // Open codec
    if(avcodec_open2(avcodecContext, avcodec, NULL) < 0)
    {
        printf("Could not open codec.\n");
        return -1;
    }

    AVFrame * avframe = av_frame_alloc();


    //FIX:Some Codec's Context Information is missing
    int64_t in_channel_layout = av_get_default_channel_layout(avcodecContext->channels);
    //Swr

    struct SwrContext * swr = swr_alloc();

    av_opt_set_int(swr, "ich", avcodecContext->channels, 0);
    av_opt_set_int(swr, "och", out_channels, 0);

    av_opt_set_int(swr, "in_channel_layout", in_channel_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", avcodecContext->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", avcodecContext->sample_fmt, 0);

    av_opt_set_int(swr, "out_channel_layout", out_channel_layout, 0);
    av_opt_set_int(swr, "out_sample_rate", out_sample_rate, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", out_sample_fmt, 0);

    //swr = swr_alloc_set_opts(swr, out_channel_layout, out_sample_fmt, out_sample_rate,
    //    in_channel_layout, avcodecContext->sample_fmt, avcodecContext->sample_rate, 0, NULL);
    swr_init(swr);

    DWORD tBeg = 0;
    int averr = 0;
    AVPacket * avpacket = (AVPacket *)av_malloc(sizeof(AVPacket));
    //Play
    if(!tBeg)
        tBeg = timeGetTime();

    while(true)
    {
        av_init_packet(avpacket);
        averr = av_read_frame(avformatContext, avpacket);
        if(averr)
            break;
        if(avpacket->stream_index != audioStream)
            continue;

        if(avpacket->dts == AV_NOPTS_VALUE)
            continue;

        printf("index:%5d\t pts:%lld\t packet size:%d", index++, avpacket->pts, avpacket->size);

        averr = avcodec_send_packet(avcodecContext, avpacket);
        if(averr)
        {
            printf("Error in avcodec_send_packet.\n");
            //return -1;
            break;
        }

        while(true)
        {
            averr = avcodec_receive_frame(avcodecContext, avframe);
            if(averr == AVERROR(EAGAIN))
                break;

            if(averr < 0)
            {
                //char buff[1024];
                //av_strerror(averr, buff, 1024);
                printf("Error in avcodec_receive_frame audio frame.\n");
                break;
            }

            while(decodeIndex - playIndex >= MAX_BUFFER - 1)
                SDL_Delay(1);
            buffer_t & buffer = buffers[decodeIndex% MAX_BUFFER];
            if(!buffer.data)
                buffer.data = (uint8_t *)av_malloc(max_frame_size * out_channels);
            buffer.len = 0;
            buffer.pos = 0;

            if(out_sample_rate == avcodecParameters->sample_rate)
            {
                int unit_size = av_get_bytes_per_sample(out_sample_fmt);
                if(av_sample_fmt_is_planar(avcodecContext->sample_fmt))
                {
                    uint8_t * dst = buffer.data;
                    for(int isam = 0; isam < avframe->nb_samples; ++isam)
                    {
                        for(int ich = 0; ich < out_channels; ++ich)
                        {
                            uint8_t * src = avframe->data[ich] + isam * unit_size;
                            memcpy(dst, src, unit_size);
                            dst += unit_size;
                        }
                    }
                    buffer.len = unit_size * avframe->nb_samples *out_channels;
                }
                else
                {
                    int out_buffer_size = avframe->linesize[0];
                    memcpy(buffer.data, avframe->data[0], out_buffer_size);
                    buffer.len += out_buffer_size;
                }
            }
            else
            {
                averr = swr_convert(swr, &buffer.data, max_frame_size, (const uint8_t **)avframe->data, avframe->nb_samples);
                if(averr < 0)
                {
                    printf("Error in swr_convert audio frame.\n");
                    break;
                }

                int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, averr, out_sample_fmt, 1);
                buffer.len = out_buffer_size;
            }
            printf("\tbuffer size:%d", buffer.len);
            InterlockedAdd(&decodeIndex, 1);
            InterlockedAdd(&g_indexB, 1);
            printf("\ %d - %d\n", g_indexA, g_indexB);
            SDL_PauseAudioDevice(adid, 0);
        }
    }
    av_packet_unref(avpacket);

    bool done = false;
    while(!done)
    {
        SDL_Event event;

        while(SDL_PollEvent(&event) > 0)
        {
            if(event.type == SDL_QUIT)
            {
                done = 1;
            }
            if((event.type == SDL_AUDIODEVICEADDED && !event.adevice.iscapture) ||
                (event.type == SDL_AUDIODEVICEREMOVED && !event.adevice.iscapture))
            {
                done = 1;
            }
        }
        SDL_Delay(1);
    }

    //while(playIndex < decodeIndex)
    //    SDL_Delay(1);

    DWORD diff = timeGetTime() - tBeg;
    DWORD diff2 = timeGetTime() - tBeg - dur / (AV_TIME_BASE / 1000);
    printf("Diff = %u.", diff);


    system("pause");
    swr_free(&swr);

#if USE_SDL
    SDL_CloseAudio();//Close SDL
    SDL_Quit();
#endif
    // Close file
#if OUTPUT_PCM
    fclose(pFile);
#endif
    for(int cnt = 0; cnt < MAX_BUFFER; ++cnt)
        av_free(buffers[cnt].data);
    // Close the codec
    avcodec_close(avcodecContext);
    // Close the video file
    avformat_close_input(&avformatContext);

    return 0;
}


