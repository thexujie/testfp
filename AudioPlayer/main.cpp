#include "stdafx.h"
#include "AudioPlayer.h"

int testPlay();
int _tmain(int argc, char* argv[])
{
    //testPlay();
    SetConsoleOutputCP(CP_UTF8);
    AudioPlayer ap;
    ap.init();
    //��·�ϳ�
    ap.playAudio("../res/musics/weibokong.wav");
    ap.playAudio("../res/musics/sample.wav");
    ap.playAudio("../res/musics/mlsx.mp3");
    ap.playAudio("../res/musics/weibokong.flac");

    Sleep(99999999);
    return 0;
}


