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
    //ap.playAudio("../res/musics/sample.wav");
    //ap.playAudio("../res/musics/mlsx.mp3");
    //ap.playAudio("../res/temp/weibokong.flac");
    ap.playAudio("../res/temp/周杰伦 - 稻香.m4a");
    //ap.playAudio("../res/temp/flower.wav");
    //ap.playAudio("../res/temp/alone.ape");
    ap.playAudio("F:\\files\\videos\\绑架者.mkv");

    Sleep(99999999);
    return 0;
}


