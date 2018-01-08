#pragma once

class DwCriticalSection
{
public:
    DwCriticalSection() { InitializeCriticalSection(&m_criticalSection); }
    ~DwCriticalSection() { DeleteCriticalSection(&m_criticalSection); }

    inline void lock()const { EnterCriticalSection((CRITICAL_SECTION*)&m_criticalSection); }
    inline void unlock()const { LeaveCriticalSection((CRITICAL_SECTION*)&m_criticalSection); }
    inline bool tryLock()const { return TryEnterCriticalSection((CRITICAL_SECTION*)&m_criticalSection) ? true : false; }

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

    int stream_index;
    int out_nb_samples_preffer;
};

//不能小于 3
const int MAX_BUFFER = 3;

struct audio_buffer
{
    int index;
    byte * data;
    int cap;
    int len;
    int pos;
    int samples;
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
    audio_context context;
    audio_play_state state;

    long long presenter_start;

    long playIndex;
    long decodeIndex;
    audio_buffer buffers[MAX_BUFFER];

    struct AVPacket * avpacket;
    int buffer_index;
    int packet_index;
    int frame_index;
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

    DwCriticalSection m_csPlayList;
    DwCriticalSection m_csPlayMix;

    AVSampleFormat m_outSampleFormat;
    int m_outSampleRate;
    int m_outBufferSamples;
    int m_outChannels;
    //sdl
    SDL_AudioDeviceID m_sdlDeviceId;
    SDL_AudioFormat m_sdlSampleFormat;

    std::vector<audio_play_conntext> sdl_datas;
};
