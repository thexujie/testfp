#include "stdafx.h"
#include "VideoPlayer.h"
#include "VideoRender.h"

#include "MediaPlayerFP.h"

int testPlay();
int _tmain2(int argc, char* argv[])
{
    av_register_all();
    MediaDemuxerFP mdfp;
    mdfp.LoadFromFile(u8"F:\\files\\videos\\绑架者.mkv");
    return 0;
}
int _tmain(int argc, char* argv[])
{
    ////testPlay();
    //SetConsoleOutputCP(CP_UTF8);
    _CrtMemState stateOld, stateNew, stateDiff;
    _CrtMemCheckpoint(&stateOld);
    _tmain2(argc, argv);
    _CrtMemCheckpoint(&stateNew);
    if(_CrtMemDifference(&stateDiff, &stateOld, &stateNew))
        _CrtMemDumpAllObjectsSince(&stateDiff);
    return 0;

    VideoPlayer ap;
    ap.init();
    ////多路合成
    ////ap.play("../res/musics/sample.wav");
    ////ap.play("../res/musics/mlsx.mp3");
    ////ap.play("../res/temp/weibokong.flac");
    ////ap.play("../res/temp/周杰伦 - 稻香.m4a");
    ////ap.play("../res/temp/flower.wav");
    ////ap.play("../res/temp/alone.ape");
    ap.play(u8"F:\\files\\videos\\bjz.mkv");
    
    XuVideoRenderD3D9 render;
    render.init(&ap);
    render.start();

    MSG msg;

    // 主消息循环: 
    while(GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}


