/**
* 最简单的基于FFmpeg的音频播放器 2
* Simplest FFmpeg Audio Player 2
*
* 雷霄骅 Lei Xiaohua
* leixiaohua1020@126.com
* 中国传媒大学/数字电视技术
* Communication University of China / Digital TV Technology
* http://blog.csdn.net/leixiaohua1020
*
* 本程序实现了音频的解码和播放。
* 是最简单的FFmpeg音频解码方面的教程。
* 通过学习本例子可以了解FFmpeg的解码流程。
*
* 该版本使用SDL 2.0替换了第一个版本中的SDL 1.0。
* 注意：SDL 2.0中音频解码的API并无变化。唯一变化的地方在于
* 其回调函数的中的Audio Buffer并没有完全初始化，需要手动初始化。
* 本例子中即SDL_memset(stream, 0, len);
*
* This software decode and play audio streams.
* Suitable for beginner of FFmpeg.
*
* This version use SDL 2.0 instead of SDL 1.2 in version 1
* Note:The good news for audio is that, with one exception,
* it's entirely backwards compatible with 1.2.
* That one really important exception: The audio callback
* does NOT start with a fully initialized buffer anymore.
* You must fully write to the buffer in all cases. In this
* example it is SDL_memset(stream, 0, len);
*
* Version 2.0
*/
#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
//Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "SDL.h"
};
#else
//Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#ifdef __cplusplus
};
#endif
#endif


const int SAMPLE_RATE = 44100;
const int MAX_AUDIO_FRAME_SIZE = SAMPLE_RATE * 4; // 1 second of 48khz 32bit audio

//Output PCM
#define OUTPUT_PCM 0
//Use SDL
#define USE_SDL 1

extern "C" { FILE __iob_func[3] = { *stdin, *stdout, *stderr }; }

//Buffer:
//|-----------|-------------|
//chunk-------pos---len-----|
static  Uint8  *audio_chunk;
static  Uint32  audio_len;
static  Uint8  *audio_pos;

/* The audio function callback takes the following parameters:
* stream: A pointer to the audio buffer to be filled
* len: The length (in bytes) of the audio buffer
*/
void  fill_audio(void *udata, Uint8 *stream, int len) {
    //SDL 2.0
    SDL_memset(stream, 0, len);
    if(audio_len == 0)		/*  Only  play  if  we  have  data  left  */
        return;
    len = (len > audio_len ? audio_len : len);	/*  Mix  as  much  data  as  possible  */

    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}
//-----------------


#pragma comment(lib, "avutil.lib")
int _tmain(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    int				i, audioStream;
    uint8_t			*out_buffer;
    int ret;
    uint32_t len = 0;
    int index = 0;
    int64_t in_channel_layout;

    FILE *pFile = NULL;
    char url[] = "mlsx.mp3";

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
    audioStream = -1;
    for(i = 0; i < avformatContext->nb_streams; i++)
    {
        if(avformatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioStream = i;
            break;
        }
    }

    if(audioStream == -1)
    {
        printf("Didn't find a audio stream.\n");
        return -1;
    }

    // Get a pointer to the codec context for the audio stream
    AVCodecContext	* avcodecContext = avformatContext->streams[audioStream]->codec;

    // Find the decoder for the audio stream
    AVCodec * avcodec = avcodec_find_decoder(avcodecContext->codec_id);
    if(avcodec == NULL)
    {
        printf("Codec not found.\n");
        return -1;
    }

    // Open codec
    if(avcodec_open2(avcodecContext, avcodec, NULL) < 0)
    {
        printf("Could not open codec.\n");
        return -1;
    }


#if OUTPUT_PCM
    pFile = fopen("output.pcm", "wb");
#endif

    AVPacket * avpacket = (AVPacket *)av_malloc(sizeof(AVPacket));
    av_init_packet(avpacket);

    //Out Audio Param
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    //nb_samples: AAC-1024 MP3-1152
    int out_nb_samples = avcodecContext->frame_size;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    //Out Buffer Size
    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);

    out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
    AVFrame * avframe = av_frame_alloc();


    int out_sample_rate = SAMPLE_RATE;
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

    //FIX:Some Codec's Context Information is missing
    in_channel_layout = av_get_default_channel_layout(avcodecContext->channels);
    //Swr

    struct SwrContext * swr = swr_alloc();
    swr = swr_alloc_set_opts(swr, out_channel_layout, out_sample_fmt, out_sample_rate,
        in_channel_layout, avcodecContext->sample_fmt, avcodecContext->sample_rate, 0, NULL);
    swr_init(swr);

    while(av_read_frame(avformatContext, avpacket) >= 0)
    {
        if(avpacket->stream_index == audioStream)
        {
			int got_picture = 0;
            ret = avcodec_decode_audio4(avcodecContext, avframe, &got_picture, avpacket);
            if(ret < 0)
            {
                printf("Error in decoding audio frame.\n");
                return -1;
            }
            if(got_picture > 0)
            {
                swr_convert(swr, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)avframe->data, avframe->nb_samples);
#if 1
                printf("index:%5d\t pts:%lld\t packet size:%d\n", index, avpacket->pts, avpacket->size);
#endif


#if OUTPUT_PCM
                //Write PCM
                fwrite(out_buffer, 1, out_buffer_size, pFile);
#endif
                index++;
            }

#if USE_SDL
            while(audio_len > 0)//Wait until finish
                SDL_Delay(1);

            //Set audio buffer (PCM data)
            audio_chunk = (Uint8 *)out_buffer;
            //Audio buffer length
            audio_len = out_buffer_size;
            audio_pos = audio_chunk;

            //Play
            SDL_PauseAudio(0);
#endif
        }
        av_free_packet(avpacket);
    }

    swr_free(&swr);

#if USE_SDL
    SDL_CloseAudio();//Close SDL
    SDL_Quit();
#endif
    // Close file
#if OUTPUT_PCM
    fclose(pFile);
#endif
    av_free(out_buffer);
    // Close the codec
    avcodec_close(avcodecContext);
    // Close the video file
    avformat_close_input(&avformatContext);

    return 0;
}


