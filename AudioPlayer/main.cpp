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
    //ap.playAudio("../temp/musics/mlsx.mp3");
    //ap.playAudio("../res/temp/flower.wav");
    ap.playAudio("../temp/hckz.ape");
    //ap.playAudio("F:\\files\\videos\\�����.mkv");
    //ap.playAudio("F:\\files\\videos\\ww.mkv");

    Sleep(99999999);
    return 0;
}


