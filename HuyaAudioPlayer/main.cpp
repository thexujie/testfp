#include "stdafx.h"
#include <stdio.h>
#include  "AudioPlayer.h"
int _tmain(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    AudioPlayer ap;
    ap.playAudio("../res/musics/weibokong.wav");
    ap.playAudio("../res/musics/sample.wav");
    ap.playAudio("../res/musics/mlsx.mp3");
    //ap.playAudio("../res/musics/weibokong.flac");
    Sleep(99999999);
    return 0;
}


