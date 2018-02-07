#pragma once

#include <queue>
#include "CSObject.h"
#include "IMedia.h"

#define SDL_PLAY 1
//#define AP_LOG

enum APError
{
    APErrorNone = 0,
    APErrorNotReady,
    APErrorInvalidParameters,
    APErrorGeneric,
    APErrorNullptr,
    APErrorNoDecoder,
    APErrorInternal,
    APErrorSDL,
};

struct audio_context
{
    int streamIndex;

    std::string filename;

    struct AVFormatContext * avformatContext;
    struct AVStream * avStream;
    struct AVCodecContext * avcodecContext;
    struct SwrContext * swr;

    struct AVCodecParameters * avcodecParameters;
    struct AVCodec * avcodec;

    //��ǰ֡����������
    long long sampleIndex;
};

struct video_context
{
    int streamIndex;
    struct AVFormatContext * avformatContext;
    struct AVStream * avStream;
    struct AVCodecContext * avcodecContext;
    struct SwsContext * sws;

    struct AVCodecParameters * avcodecParameters;
    struct AVCodec * avcodec;
};

//����С�� 3
const int MAX_AUDIO_BUFFER = 3;
const int MAX_VIDEO_BUFFER = 3;

struct audio_buffer
{
    long long index;
    byte * data;
    long long sampleIndex;
    long long sampleSize;

    //���ֵ
    long long dts;
};

struct video_buffer
{
    long long index;
    byte * data;
    int width;
    int height;
    int pitch;

    //���ֵ
    long long dts;
    long long duration;
};

enum audio_play_state
{
    audio_play_state_error = -1,
    audio_play_state_ok = 0,
    audio_play_state_decode_end,
    audio_play_state_end,
};

struct player_conntext
{
    audio_play_state state;

    audio_context audio;
    video_context video;

    struct AVPacket * audioPacket;
    struct AVPacket * videoPacket;
    struct AVFrame * audioFrame;
    struct AVFrame * videoFrame;

    //ʱ����ͬ��
    long long dtsBase;

    //���š�����
    audio_buffer audioBuffers[MAX_AUDIO_BUFFER];
    long long audioPlayIndex;
    long long audioDecodeIndex;
    long long dtsAudio;
    long long ptsAudio;
    long long ptsAudioOffset;

    //���š�����
    video_buffer videoBuffers[MAX_VIDEO_BUFFER];
    long long videoPlayIndex;
    long long videoDecodeIndex;
    long long dtsVideo;
    long long ptsVideo;
    long long ptsVideoOffset;


    //���뵽�Ļ���
    long long audioBufferIndex;
    long long videoBufferIndex;
    //��
    long long packetIndex;
    //֡
    long long audioFrameIndex;
    long long videoFrameIndex;
    //����
    long long audioSampleIndex;
    long long videoSampleIndex;

    std::queue<struct AVPacket *> audioPackets;
    std::queue<struct AVPacket *> videoPackets;
};

class VideoPlayer : public IVideoPlayer
{
public:
    VideoPlayer();
    ~VideoPlayer();

    int init();
    int play(const char * filename);

private:
    int initSDL();
    int generate(std::string filename, audio_context & audioContext, video_context & videoContext);

    int doPlay();
    void doMix(Uint8 * stream, int len);
    int doCombine(char * data, int width, int height, int pitch, int & duration);

    static unsigned __stdcall playThread(void * args);
    static void SDLCALL sdlMix(void * udata, Uint8 * stream, int len);
private:
    HANDLE m_hPlayThread;
    long m_endPlayThread;
    std::vector<std::string> m_playList;

    CSObject m_csPlayList;
    CSObject m_csPlayMix;

    AVSampleFormat m_outSampleFormat;
    int m_outSampleRate;
    int m_outBufferSamples;
    int m_outChannels;
    //sdl
    SDL_AudioDeviceID m_sdlDeviceId;
    SDL_AudioFormat m_sdlSampleFormat;

    AVPixelFormat m_outPixelFormat;

    std::vector<player_conntext> sdl_datas;
#ifdef AP_LOG
    FILE * m_logFile;
#endif
};
