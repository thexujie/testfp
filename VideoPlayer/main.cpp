#include "stdafx.h"
#include "MediaPlayerFP.h"
#include "MMAudioPlayerFP.h"

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <Windows.h>

std::mutex m;
std::condition_variable cv;
std::string data;
bool ready = false;
bool processed = false;

int _tmain2(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_ACP);
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE)
    {
        hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    }

    if (hr == S_FALSE)
        return S_OK;

    av_register_all();
    //av_log_set_level(AV_LOG_TRACE);

    std::shared_ptr<IFFmpegDemuxer> demuxer = std::make_shared<MediaDemuxerFP>();
    //demuxer->LoadFromFile("F:\\files\\videos\\lig.mp4");
    //demuxer->LoadFromFile("F:\\files\\videos\\绑架者.mkv");
    demuxer->LoadFromFile("F:\\files\\videos\\ww.mkv");
    //demuxer->LoadFromFile("../res/temp/weibokong.flac");
    auto types = demuxer->GetStreamTypes();
    int32_t audioIndex = -1;
    for each (auto iter in types)
    {
        if(iter.second == AVMEDIA_TYPE_AUDIO)
        {
            audioIndex = iter.first;
            break;
        }
    }

    //while (audioIndex >= 0)
    //{
    //    auto packet = demuxer->NextPacket(audioIndex);
    //    if (!packet.ptr)
    //        break;

    //    printf("packet [%lld] size=%d\n", packet.localIndex, packet.ptr->size);
    //    Sleep(100);
    //}
    //Sleep(9999999);

    std::shared_ptr<IAudioPacketStreamFP> audioStream = demuxer->GetAudioStream(audioIndex);
    if (!audioStream)
        return 0;

    std::shared_ptr<IAudioDecoderFP> audioDecoder = std::make_shared<AudioDecoderFP>(audioStream);

    //int frameIndex = 0;
    //while (frameIndex >= 0)
    //{
    //    auto frame = audioDecoder->NextFrame();
    //    if (!frame.ptr)
    //    {
    //        printf("xxxxxxxxxxxxxxxxxxxxxxxxxxx ended!");
    //        break;
    //    }
    //    printf("frame [%lld][%d]\n", frame.index, frameIndex++);
    //    //Sleep(100);
    //}
    //Sleep(9999999);


    std::shared_ptr<MMAudioPlayerFP> player = std::make_shared<MMAudioPlayerFP>(audioDecoder);
    auto devices = player->GetDeviceDescs();
    player->Start();
    player->WaitForStop();

    return 0;
}

int _tmain(int argc, char* argv[])
{
    _CrtMemState stateOld, stateNew, stateDiff;
    _CrtMemCheckpoint(&stateOld);
    _tmain2(argc, argv);
    _CrtMemCheckpoint(&stateNew);
    if(_CrtMemDifference(&stateDiff, &stateOld, &stateNew))
        _CrtMemDumpAllObjectsSince(&stateDiff);
    return 0;
}


