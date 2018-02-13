#include "stdafx.h"
#include "AudioPlayer.h"

int testPlay();
int _tmain(int argc, char* argv[])
{
    //testPlay();
    //SetConsoleOutputCP(CP_UTF8);
    AudioPlayer ap;
    ap.init();
    //多路合成
    //ap.playAudio("../temp/musics/mlsx.mp3");
    //ap.playAudio("../res/temp/flower.wav");
    ap.playAudio("../temp/hckz.ape");
    //ap.playAudio("F:\\files\\videos\\绑架者.mkv");
    //ap.playAudio("F:\\files\\videos\\ww.mkv");

    Sleep(99999999);
    return 0;
}


