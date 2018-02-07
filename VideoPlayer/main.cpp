#include "stdafx.h"
#include "MediaPlayerFP.h"
#include "DSoundAudioPlayerFP.h"

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

void worker_thread()
{
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk);

    // after the wait, we own the lock.
    std::cout << "Worker thread is processing data\n";
    data += " after processing";

    // Send data back to main()
    processed = true;
    std::cout << "Worker thread signals data processing completed\n";

    // Manual unlocking is done before notifying, to avoid waking up
    // the waiting thread only to block again (see notify_one for details)
    lk.unlock();
    cv.notify_one();
}

int _tmain2(int argc, char* argv[])
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE)
    {
        hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    }

    /* S_FALSE means success, but someone else already initialized. */
    /* You still need to call CoUninitialize in this case! */
    if (hr == S_FALSE)
    {
        return S_OK;
    }

    av_register_all();
    
    std::shared_ptr<IFFmpegDemuxer> demuxer = std::make_shared<MediaDemuxerFP>();
    //demuxer->LoadFromFile("F:\\files\\videos\\lig.mp4");
    demuxer->LoadFromFile("F:\\files\\videos\\绑架者.mkv");
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

    std::shared_ptr<IAudioDecoderFP> audioDecoder = std::make_shared<AudioDecoderFP>(demuxer, audioIndex);

    int frameIndex = 0;
    while (frameIndex >= 0)
    {
        auto frame = audioDecoder->NextFrame();
        if (!frame.ptr)
            break;
        printf("frame [%lld][%d]\n", frame.index, frameIndex++);
        //Sleep(100);
    }
    Sleep(9999999);


    std::shared_ptr<DSoundAudioPlayerFP> player = std::make_shared<DSoundAudioPlayerFP>(audioDecoder);
    auto devices = player->GetDeviceDescs();
    player->Start();
    player->WaitForStop();

    return 0;
}

int _tmain(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    _CrtMemState stateOld, stateNew, stateDiff;
    _CrtMemCheckpoint(&stateOld);
    _tmain2(argc, argv);
    _CrtMemCheckpoint(&stateNew);
    if(_CrtMemDifference(&stateDiff, &stateOld, &stateNew))
        _CrtMemDumpAllObjectsSince(&stateDiff);
    return 0;
}


