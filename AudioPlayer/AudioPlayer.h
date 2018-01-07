#pragma once

class AudioPlayer
{
public:
    AudioPlayer();
    ~AudioPlayer();

    void play(std::string filename);

private:
    void doPlay();
    void initSDL();
    static unsigned __stdcall playThread(void * args);
private:
    HANDLE m_hPlayThread;
    std::vector<std::string> m_playList;
};
