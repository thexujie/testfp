#include "stdafx.h"
#include "AudioPlayer.h"
#include <process.h>

AudioPlayer::AudioPlayer()
{

}

AudioPlayer::~AudioPlayer()
{

}

void AudioPlayer::play(std::string filename)
{
    m_playList.push_back(filename);

    if(!m_hPlayThread)
        m_hPlayThread = (HANDLE)_beginthreadex(0, 0, playThread, (void *)this, 0, NULL);
}

void AudioPlayer::doPlay()
{
    initSDL();
}

void AudioPlayer::doMix()
{
    
}

void AudioPlayer::initSDL()
{

}
    
unsigned AudioPlayer::playThread(void * args)
{
    AudioPlayer * pThis = (AudioPlayer * )args;
    pThis->doPlay();
    return 0;
}
