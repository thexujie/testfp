#pragma once

class CSObject
{
public:
    CSObject() { InitializeCriticalSection(&m_criticalSection); }
    ~CSObject() { DeleteCriticalSection(&m_criticalSection); }

    void lock()const { EnterCriticalSection((CRITICAL_SECTION*)&m_criticalSection); }
    void unlock()const { LeaveCriticalSection((CRITICAL_SECTION*)&m_criticalSection); }
    bool tryLock()const { return TryEnterCriticalSection((CRITICAL_SECTION*)&m_criticalSection) ? true : false; }

private:
    CRITICAL_SECTION m_criticalSection;
};


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
    std::string filename;

    struct AVFormatContext * avformatContext;
    struct AVCodecContext * avcodecContext;
    struct SwrContext * swr;

    struct AVCodecParameters * avcodecParameters;
    struct AVCodec * avcodec;

    int audioStreamIndex;
    int out_nb_samples_preffer;
    //当前帧的样本索引
    long long sampleIndex;
};

//不能小于 3
const int MAX_BUFFER = 2;

struct audio_buffer
{
    long long index;
    byte * data;
    long long sampleIndex;
    long long sampleSize;

    //相对值
    long long pts;
};

enum audio_play_state
{
    audio_play_state_error = -1,
    audio_play_state_ok = 0,
    audio_play_state_decode_end,
    audio_play_state_end,
};

struct audio_play_conntext
{
    audio_play_state state;

    audio_context context;
    struct AVPacket * avpacket;

    audio_buffer buffers[MAX_BUFFER];

    //播放、解码
    long playIndex;
    long decodeIndex;

    //时间轴同步
    long long pts;
    long long ptsBase;
    long long ptsAdjust;
    long long ptsOffset;


    //解码到的缓冲
    long long bufferIndex;
    //包
    long long packetIndex;
    //帧
    long long frameIndex;
    //样本
    long long sampleIndex;
};

class AudioPlayer
{
public:
    AudioPlayer();
    ~AudioPlayer();

    int init();
    int playAudio(const char * filename);

private:
    int initSDL();
    int generate(std::string filename, audio_context & context);

    int doPlay();
    void doMix(Uint8 * stream, int len);

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

    std::vector<audio_play_conntext> sdl_datas;
};
