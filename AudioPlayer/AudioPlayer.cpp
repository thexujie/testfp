#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Output PCM
#define OUTPUT_PCM 0
//Use SDL
#define USE_SDL 1

struct buffer_t
{
    byte * data;
    int len;
    int pos;
};

const int MAX_BUFFER = 2;
long decodeIndex = 0;
long playIndex = 0;
buffer_t buffers[MAX_BUFFER];

void  fill_audio(void *udata, Uint8 *stream, int len) {
    //SDL 2.0
    SDL_memset(stream, 0, len);
    if(playIndex >= decodeIndex)
        return;

    buffer_t & buffer = buffers[playIndex % MAX_BUFFER];
    if(buffer.len == 0)
        return;
    len = (len > buffer.len ? buffer.len : len);	/*  Mix  as  much  data  as  possible  */

    SDL_MixAudio(stream, buffer.data + buffer.pos, len, SDL_MIX_MAXVOLUME);
    buffer.pos += len;
    buffer.len -= len;

    if(buffer.len == 0)
        InterlockedAdd(&playIndex, 1);
}

int _tmain(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    memset(buffers, 0, sizeof(buffers));
    int index = 0;

    //char url[] = "../res/musics/weibokong.wav";
    char url[] = "../res/musics/sample.wav";
    //char url[] = "../res/musics/mlsx.mp3";

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

    //Out Audio Param
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    int out_sample_rate = avcodecParameters->sample_rate;
    //out_sample_rate = 192000;

    int out_nb_samples = 0;

    switch(avcodecParameters->codec_id)
    {
    case AV_CODEC_ID_ADPCM_MS:
        out_nb_samples = 2 + 2 * (avcodecParameters->block_align- 7 * avcodecParameters->channels) / avcodecParameters->channels;
        break;
    case AV_CODEC_ID_FIRST_AUDIO:
        out_nb_samples = avcodecParameters->bit_rate / 20;
        if(out_nb_samples < 1)
            out_nb_samples = 1;
        break;
    default:
        out_nb_samples = av_rescale_rnd(avcodecParameters->frame_size, out_sample_rate, avcodecParameters->sample_rate, AV_ROUND_UP);
        break;
    }
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    //Out Buffer Size

    int max_frame_size = out_sample_rate * 4;

    AVFrame * avframe = av_frame_alloc();


    //FIX:Some Codec's Context Information is missing
    int64_t in_channel_layout = av_get_default_channel_layout(avcodecContext->channels);
    //Swr

    struct SwrContext * swr = swr_alloc();

    //av_opt_set_int(swr, "ich", avcodecContext->channels, 0);
    //av_opt_set_int(swr, "och", out_channels, 0);

    //av_opt_set_int(swr, "in_channel_layout", in_channel_layout, 0);
    //av_opt_set_int(swr, "in_sample_rate", avcodecContext->sample_rate, 0);
    //av_opt_set_sample_fmt(swr, "in_sample_fmt", avcodecContext->sample_fmt, 0);

    //av_opt_set_int(swr, "out_channel_layout", out_channel_layout, 0);
    //av_opt_set_int(swr, "out_sample_rate", out_sample_rate, 0);
    //av_opt_set_sample_fmt(swr, "out_sample_fmt", out_sample_fmt, 0);

    swr = swr_alloc_set_opts(swr, out_channel_layout, out_sample_fmt, out_sample_rate,
        in_channel_layout, avcodecContext->sample_fmt, avcodecContext->sample_rate, 0, NULL);
    swr_init(swr);

    //SDL------------------
#if USE_SDL
    //Init
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    //SDL_AudioSpec
    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = out_sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = out_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = out_nb_samples;
    wanted_spec.callback = fill_audio;
    wanted_spec.userdata = avcodecContext;

    if(SDL_OpenAudio(&wanted_spec, NULL) < 0)
    {
        printf("can't open audio.\n");
        return -1;
    }
#endif

    DWORD tBeg = 0;
    int averr = 0;
    AVPacket * avpacket = (AVPacket *)av_malloc(sizeof(AVPacket));
    while(true)
    {
        av_init_packet(avpacket);
        averr = av_read_frame(avformatContext, avpacket);
        if(averr)
            break;

        printf("index:%5d\t pts:%lld\t packet size:%d\n", index++, avpacket->pts, avpacket->size);

        if(avpacket->stream_index == audioStream)
        {
            if(av_sample_fmt_is_planar(avcodecContext->sample_fmt))
            {
                averr = avcodec_send_packet(avcodecContext, avpacket);
                if(averr)
                {
                    printf("Error in avcodec_send_packet.\n");
                    //return -1;
                    break;
                }
                averr = avcodec_receive_frame(avcodecContext, avframe);
                if(averr < 0)
                {
                    printf("Error in decoding audio frame.\n");
                    //return -1;
                    break;
                }

                while(decodeIndex - playIndex >= MAX_BUFFER - 1)
                    SDL_Delay(1);
                buffer_t & buffer = buffers[decodeIndex% MAX_BUFFER];
                if(!buffer.data)
                    buffer.data = (uint8_t *)av_malloc(max_frame_size * out_channels);

                swr_convert(swr, &buffer.data, max_frame_size, (const uint8_t **)avframe->data, avframe->nb_samples);

                int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);
                buffer.len = out_buffer_size;
                buffer.pos = 0;
            }
            else
            {
                while(decodeIndex - playIndex >= MAX_BUFFER - 1)
                    SDL_Delay(1);
                buffer_t & buffer = buffers[decodeIndex% MAX_BUFFER];
                if(!buffer.data)
                    buffer.data = (uint8_t *)av_malloc(max_frame_size * out_channels);
                buffer.pos = 0;
                buffer.len = 0;

                int size = avpacket->size;
                int pos = 0;
                AVPacket packet;
                while(pos < size)
                {
                    int psize = size > avcodecParameters->block_align ? avcodecParameters->block_align : size;
                    av_init_packet(&packet);
                    packet.data = avpacket->data + pos;
                    packet.size = psize;
                    pos += psize;

                    averr = avcodec_send_packet(avcodecContext, &packet);
                    if(averr)
                    {
                        printf("Error in avcodec_send_packet.\n");
                        //return -1;
                        break;
                    }
                    averr = avcodec_receive_frame(avcodecContext, avframe);
                    if(averr < 0)
                    {
                        printf("Error in decoding audio frame.\n");
                        //return -1;
                        break;
                    }

                    //memcpy(buffer.data, avframe->data[0], avframe->linesize[0]);
                    //buffer.len = avframe->linesize[0];
                    //buffer.pos = 0;

                    uint8_t * out = buffer.data + buffer.len;
                    averr = swr_convert(swr, &out, max_frame_size - buffer.len, (const uint8_t **)avframe->data, avframe->nb_samples);
                    if(averr < 0)
                    {
                        printf("Error in swr_convert audio frame.\n");
                        //return -1;
                        break;
                    }

                    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);
                    buffer.len += out_buffer_size;
                }
            }

            InterlockedAdd(&decodeIndex, 1);
            //Play
            if(!tBeg)
                tBeg = timeGetTime();
            SDL_PauseAudio(0);
        }
    }
    av_packet_unref(avpacket);

    while(1)
        SDL_Delay(1);
    DWORD diff = timeGetTime() - tBeg - dur / (AV_TIME_BASE / 1000);
    printf("Diff = %u.", diff);

    while(playIndex <= decodeIndex)
        SDL_Delay(1);

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


