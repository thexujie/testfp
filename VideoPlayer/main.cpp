#include "stdafx.h"
#include "VideoPlayer.h"
#include "VideoRender.h"
int testPlay();
int _tmain(int argc, char* argv[])
{
    ////testPlay();
    ////SetConsoleOutputCP(CP_UTF8);
    VideoPlayer ap;
    ap.init();
    ////��·�ϳ�
    ////ap.play("../res/musics/sample.wav");
    ////ap.play("../res/musics/mlsx.mp3");
    ////ap.play("../res/temp/weibokong.flac");
    ////ap.play("../res/temp/�ܽ��� - ����.m4a");
    ////ap.play("../res/temp/flower.wav");
    ////ap.play("../res/temp/alone.ape");
    ap.play("F:\\files\\videos\\bjz.mkv");
    
    XuVideoRenderD3D9 render;
    render.init(&ap);
    render.start();

    MSG msg;

    // ����Ϣѭ��: 
    while(GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}


