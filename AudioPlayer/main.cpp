#include "stdafx.h"
#include "AudioPlayer.h"

int testPlay();
int _tmain(int argc, char* argv[])
{
    //testPlay();
    //SetConsoleOutputCP(CP_UTF8);
    AudioPlayer ap;
    ap.init();
    //��·�ϳ�
    //ap.playAudio("../res/musics/sample.wav");
    //ap.playAudio("../res/musics/mlsx.mp3");
    //ap.playAudio("../res/temp/weibokong.flac");
    ap.playAudio("../res/temp/�ܽ��� - ����.m4a");
    //ap.playAudio("../res/temp/flower.wav");
    //ap.playAudio("../res/temp/alone.ape");
    ap.playAudio("F:\\files\\videos\\�����.mkv");

    Sleep(99999999);
    return 0;
}


