#include "stdafx.h"

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <Windows.h>
#include <combaseapi.h>

#include "FFmpegDemuxer.h"
#include "FFmpegAudioDecoder.h"
#include "FFmpegVideoDecoder.h"
#include "MMAudioPlayer.h"
#include "VideoRenderWindow.h"
#include "D3D9SurfaceVideoPlayer.h"

std::mutex m;
std::condition_variable cv;
std::string data;
bool ready = false;
bool processed = false;

std::shared_ptr<IAudioBufferInputStream> CreateAudioStream(u8string filePath)
{
    std::shared_ptr<FFmpegDemuxer> media = std::make_shared<FFmpegDemuxer>();
    media->LoadFromFile(filePath);
    int32_t audioIndex = -1;
    auto types = media->GetStreamTypes();
    for each (auto iter in types)
    {
        if (iter.second == AVMEDIA_TYPE_AUDIO)
        {
            audioIndex = iter.first;
            break;
        }
    }
    std::shared_ptr<IAudioPacketReader> reader = media->GetAudioStream(audioIndex);
    if (!reader)
        return {};

    auto param = reader->GetAudioDecodeParam();
    std::cout << filePath << ", "
        << "<" << avcodec_get_name(param.codecId) << ">, "
        << av_get_sample_fmt_name(param.format.sampleFormat) << ", "
        << param.format.chanels << " chanels, "
        << param.format.sampleRate << "Hz"
        << std::endl;
    std::shared_ptr<IAudioBufferInputStream> audio = std::make_shared<FFmpegAudioDecoder>(reader);
    return audio;
}

std::shared_ptr<IVideoBufferInputStream> CreateVideoStream(u8string filePath)
{
    std::shared_ptr<FFmpegDemuxer> media = std::make_shared<FFmpegDemuxer>();
    media->LoadFromFile(filePath);
    int32_t streamId = -1;
    auto types = media->GetStreamTypes();
    for each (auto iter in types)
    {
        if(iter.second == AVMEDIA_TYPE_VIDEO)
        {
            streamId = iter.first;
            break;
        }
    }
    std::shared_ptr<IVideoPacketReader> reader = media->GetVideoStream(streamId);
    if(!reader)
        return {};

    auto param = reader->GetVideoDecodeParam();
    std::cout << filePath << ", "
        << "<" << avcodec_get_name(param.codecId) << ">, "
        << av_get_pix_fmt_name(param.format.pixelFormat) << ", "
        << param.format.width << " x " << param.format.height
        << std::endl;
    std::shared_ptr<IVideoBufferInputStream> video = std::make_shared<FFmpegVideoDecoder>(reader);
    VideoFormat format = {};
    format.width = param.format.width;
    format.height = param.format.height;
    format.pixelFormat = AV_PIX_FMT_BGRA;
    video->SetOutputFormat(format);
    return video;
}

void testAudio(u8string filePath)
{
    std::shared_ptr<FFmpegDemuxer> media = std::make_shared<FFmpegDemuxer>();
    media->LoadFromFile("../temp/weibokong.flac");
    auto types = media->GetStreamTypes();
    int32_t audioIndex = -1;
    for each (auto iter in types)
    {
        if (iter.second == AVMEDIA_TYPE_AUDIO)
        {
            audioIndex = iter.first;
            break;
        }
    }

    std::shared_ptr<IAudioPacketReader> reader = media->GetAudioStream(audioIndex);
    if (!reader)
        return;

    //FpPacket packet = {};
    //while (audioIndex >= 0)
    //{
    //    reader->NextPacket(packet, 9999);
    //    if (!packet.ptr)
    //        break;

    //    printf("packet [%lld] size=%d\n", packet.localIndex, packet.ptr->size);
    //}
    //Sleep(9999999);

    std::shared_ptr<IAudioBufferInputStream> audio = std::make_shared<FFmpegAudioDecoder>(reader);

    AudioBuffer buffer = {};
    int frameIndex = 0;
    while (frameIndex >= 0)
    {
        auto frame = audio->NextBuffer(buffer, 99999);
        if (!buffer.data.get())
        {
            printf("xxxxxxxxxxxxxxxxxxxxxxxxxxx ended!");
            break;
        }
        printf("frame [%lld][%d]\n", buffer.index, frameIndex++);
        //Sleep(100);
    }
    audio.reset();
}

void pushMedia(u8string filePath, std::shared_ptr<MMAudioPlayer> audioPlayer, std::shared_ptr<D3D9SurfaceVideoPlayer> videoPlayer,
    std::shared_ptr<IVideoDecoderHWAccelerator> videoForamtCooperator)
{
    std::shared_ptr<FFmpegDemuxer> media = std::make_shared<FFmpegDemuxer>();
    media->LoadFromFile(filePath);
    int32_t audioIndex = -1;
    int32_t videoIndex = -1;
    auto types = media->GetStreamTypes();
    for each (auto iter in types)
    {
        if (iter.second == AVMEDIA_TYPE_AUDIO)
        {
            audioIndex = iter.first;
            break;
        }
    }
    for each (auto iter in types)
    {
        if (iter.second == AVMEDIA_TYPE_VIDEO)
        {
            videoIndex = iter.first;
            break;
        }
    }
    std::shared_ptr<IAudioPacketReader> audioReader = media->GetAudioStream(audioIndex);
    if (!audioReader)
        return;

    auto param = audioReader->GetAudioDecodeParam();
    std::cout << filePath << ", "
        << "<" << avcodec_get_name(param.codecId) << ">, "
        << av_get_sample_fmt_name(param.format.sampleFormat) << ", "
        << param.format.chanels << " chanels, "
        << param.format.sampleRate << "Hz"
        << std::endl;
    std::shared_ptr<IAudioBufferInputStream> audioDecoder = std::make_shared<FFmpegAudioDecoder>(audioReader);

    MMDeviceDesc audioDeviceDesc = {};
    audioPlayer->GetDefaultDeviceDesc(audioDeviceDesc);
    audioDecoder->SetOutputFormat(audioDeviceDesc.format);

    std::shared_ptr<IVideoPacketReader> videoReader = media->GetVideoStream(videoIndex);
    if (!videoReader)
        return;

    auto videoParam = videoReader->GetVideoDecodeParam();
    std::cout << filePath << ", "
        << "<" << avcodec_get_name(videoParam.codecId) << ">, "
        << av_get_pix_fmt_name(videoParam.format.pixelFormat) << ", "
        << videoParam.format.width << " x " << videoParam.format.height
        << std::endl;
    std::shared_ptr<FFmpegVideoDecoder> videoDecoder = std::make_shared<FFmpegVideoDecoder>(videoReader);
    videoDecoder->SetVideoFormatCooperator(videoForamtCooperator);

    VideoFormat decodeFormat = videoDecoder->DecodeFormat();
    videoDecoder->SetOutputFormat(decodeFormat);

    auto [__,audioClock] = audioPlayer->AddAudio(audioDecoder);
    auto [_,videoClock] = videoPlayer->AddVideo(videoDecoder);
    videoPlayer->SetMasterClock(audioClock);

    audioDecoder->WaitForFrames(9999999999);
    videoDecoder->WaitForFrames(9999999999);


    //while (true)
    //{
    //    VideoBuffer buffer = {};
    //    auto state = videoDecoder->NextBuffer(buffer, 99999);
    //    if (!buffer.avframe)
    //    {
    //        printf("xxxxxxxxxxxxxxxxxxxxxxxxxxx ended!");
    //        break;
    //    }
    //    printf("\rframe [%lld][%02d:%02d:%02d.%03d]",
    //        buffer.index,
    //        (int32_t)buffer.pts / 3600,
    //        (int32_t)buffer.pts % 3600 / 60,
    //        (int32_t)buffer.pts % 60,
    //        (int32_t)(buffer.pts * 1000) % 1000);
    //    //Sleep(100);
    //}
}

void testVideo(u8string filePath)
{
    std::shared_ptr<FFmpegDemuxer> media = std::make_shared<FFmpegDemuxer>();
    media->LoadFromFile(filePath);

    int32_t streamId = -1;
    auto types = media->GetStreamTypes();
    for each (auto iter in types)
    {
        if(iter.second == AVMEDIA_TYPE_VIDEO)
        {
            streamId = iter.first;
            break;
        }
    }

    std::shared_ptr<IVideoPacketReader> reader = media->GetVideoStream(streamId);
    if(!reader)
        return;

    Packet packet{};
    reader->NextPacket(packet, std::numeric_limits<uint32_t>::max());

    auto param = reader->GetVideoDecodeParam();
    std::cout << filePath << ", "
        << "<" << avcodec_get_name(param.codecId) << ">, "
        << av_get_pix_fmt_name(param.format.pixelFormat) << ", "
        << param.format.width << " x " << param.format.height
        << std::endl;
    std::shared_ptr<FFmpegVideoDecoder> decoder = std::make_shared<FFmpegVideoDecoder>(reader);

    //std::shared_ptr<D3D9Factory> d3d9 = std::make_shared<D3D9Factory>();

    // dxva2 解码
    class VideoRenderWindowDesktop : public IVideoRenderWindow
    {
    public:
        virtual void * GetHandle() const
        {
            return (void *)GetDesktopWindow();
        }
    };
    //std::shared_ptr<D3D9SurfaceVideoPlayerDefaultDevice> playerDevice = std::make_shared<D3D9SurfaceVideoPlayerDefaultDevice>(d3d9, std::make_shared<VideoRenderWindowDesktop>());
    //decoder->SetVideoFormatCooperator(playerDevice);

    decoder->SetOutputFormat(decoder->DecodeFormat());
    decoder->WaitForFrames(300);
    return;

    while(true)
    {
        VideoBuffer buffer = {};
        auto state = decoder->NextBuffer(buffer, 99999);
        if(!buffer.avframe)
        {
            printf("xxxxxxxxxxxxxxxxxxxxxxxxxxx ended!");
            break;
        }
        printf("\rframe [%lld][%02d:%02d:%02d.%03d]",
            buffer.index,
            (int32_t)buffer.pts / 3600,
            (int32_t)buffer.pts % 3600 / 60,
            (int32_t)buffer.pts % 60,
            (int32_t)(buffer.pts * 1000) % 1000);
        //Sleep(100);
        break;
    }
}
#include <sys/stat.h>

int _tmain2(int argc, char* argv[])
{
    //SetConsoleOutputCP(CP_ACP);
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE)
    {
        hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    }

    if (hr == S_FALSE)
        return S_OK;

    av_register_all();
    //av_log_set_level(AV_LOG_VERBOSE);
    //testVideo("G:/media/videos/SHIELD05E01.mkv");

    //testAudio("../temp/weibokong.flac");
    //testVideo("F:\\files\\videos\\ww.mkv");
    //return 0;

    //testVideo("F:\\files\\videos\\SHIELD05E01.mkv");
    //return 0;

    std::shared_ptr<MMAudioPlayer> audioPlayer = std::make_shared<MMAudioPlayer>();
    //audioPlayer->AddAudio(CreateAudioStream("../temp/crbhf.ape"));
    //audioPlayer->AddAudio(CreateAudioStream("G:/media/videos/卧虎藏龙.mkv"));
    //audioPlayer->AddAudio(CreateAudioStream("G:/media/videos/电影3/新特警判官/新特警判官.mkv"));
    //audioPlayer->Start();
    //audioPlayer->WaitForStop();

    std::shared_ptr<D3D9Factory> d3d9 = std::make_shared<D3D9Factory>();
    auto deviceDescs = d3d9->GetDeviceDescs();

    std::shared_ptr<VideoRenderWindow> renderWindow = std::make_shared<VideoRenderWindow>();
    std::shared_ptr<D3D9SurfaceVideoPlayerDefaultDevice> playerDevice = std::make_shared<D3D9SurfaceVideoPlayerDefaultDevice>(d3d9, renderWindow);

    auto deviceDesc = playerDevice->GetDesc();
    auto dxvaCodecs = playerDevice->GetCodecDescs();

    std::shared_ptr<D3D9SurfaceVideoPlayer> videoPlayer = std::make_shared<D3D9SurfaceVideoPlayer>(playerDevice);

    //pushMedia("F:\\files\\videos\\ww4.mkv", audioPlayer, videoPlayer, playerDevice);
    //pushMedia("F:\\files\\videos\\ww4.rmvb", audioPlayer, videoPlayer);
    //pushMedia("F:\\files\\videos\\sxwy.rmvb", audioPlayer, videoPlayer);
    //pushMedia("F:\\files\\videos\\lig.mp4", audioPlayer, videoPlayer);
    //pushMedia("F:\\files\\videos\\绑架者.mkv", audioPlayer, videoPlayer);
    //pushMedia("F:\\files\\videos\\jgkld2017.mp4", audioPlayer, videoPlayer);
    //pushMedia("F:\\files\\videos\\aglpb.mkv", audioPlayer, videoPlayer);

    pushMedia("F:\\files\\videos\\bjky.mp4", audioPlayer, videoPlayer, playerDevice);
    //pushMedia("F:\\files\\videos\\SHIELD05E01.mkv", audioPlayer, videoPlayer, playerDevice);
    //pushMedia("G:\\media\\videos\\bjky.mp4", audioPlayer, videoPlayer, playerDevice);
    //pushMedia("G:/media/videos/SHIELD05E01.mkv", audioPlayer, videoPlayer, playerDevice);
    //pushMedia("G:/media/videos/SHIELD03E03.mkv", audioPlayer, videoPlayer, playerDevice);
    //pushMedia("G:/media/videos/2018/War.For.The.Planet.Of.The.Apes.mp4", audioPlayer, videoPlayer, playerDevice);
    //pushMedia("G:/media/videos/电影3/新特警判官/新特警判官.mkv", audioPlayer, videoPlayer, playerDevice);
	//pushMedia("G:/BaiduNetdiskDownload/bjky.mp4", audioPlayer, videoPlayer, playerDevice);
	//pushMedia("G:/BaiduNetdiskDownload/APink No No no.mp4", audioPlayer, videoPlayer, playerDevice);

	//pushMedia("F:\\files\\videos\\bjky.mp4", audioPlayer, videoPlayer, playerDevice);
	//pushMedia("G:/media/videos/SHIELD03E03.mkv", audioPlayer, videoPlayer, playerDevice);
    //pushMedia("G:/media/videos/2018/War.For.The.Planet.Of.The.Apes.mp4", audioPlayer, videoPlayer, playerDevice);
    //pushMedia("G:/media/videos/电影3/新特警判官/新特警判官.mkv", audioPlayer, videoPlayer, playerDevice);
	//pushMedia("G:/media/videos/卧虎藏龙.mkv", audioPlayer, videoPlayer, playerDevice);
	//pushMedia("G:/BaiduNetdiskDownload/APink No No no.mp4", audioPlayer, videoPlayer, playerDevice);

    //audioPlayer->AddAudio(CreateAudioStream("F:\\files\\videos\\lig.mp4"));
    //audioPlayer->AddAudio(CreateAudioStream("F:\\files\\videos\\绑架者.mkv"));
    //audioPlayer->AddAudio(CreateAudioStream("F:\\files\\videos\\ww.mkv"));
    //audioPlayer->AddAudio(CreateAudioStream("../temp/weibokong.flac"));
    //audioPlayer->AddAudio(CreateAudioStream("../temp/dukou.flac"));
    //audioPlayer->AddAudio(CreateAudioStream("../temp/sample.wav"));
    //audioPlayer->AddAudio(CreateAudioStream("../temp/blueskies.ape"));
    //audioPlayer->AddAudio(CreateAudioStream("G:/media/video/2018/Marvel's.Agents.of.S.H.I.E.L.D.S03E01.Laws.of.Nature.720p.WEB-DL.DD5.1.H.264-CtrlHD.mkv"));
    //audioPlayer->AddAudio(CreateAudioStream("G:/media/video/2018/War.For.The.Planet.Of.The.Apes.mp4"));

    //videoPlayer->AddVideo(CreateVideoStream("F:\\files\\videos\\ww.mkv"));

    audioPlayer->Start();
    videoPlayer->Start();

    MSG msg = {};
    while(GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    audioPlayer->Stop();
    videoPlayer->Stop();
    audioPlayer->WaitForStop();
    videoPlayer->WaitForStop();

    return 0;
}

int _tmain(int argc, char* argv[])
{
    //AVFormatContext * ctx = nullptr;
    //avformat_open_input(&ctx, "G:/media/videos/SHIELD05E01.mkv", NULL, NULL);
    //if (ctx)
    //    avformat_close_input(&ctx);
    _CrtMemState stateOld, stateNew, stateDiff;
    _CrtMemCheckpoint(&stateOld);
    _tmain2(argc, argv);
    _CrtMemCheckpoint(&stateNew);
    if(_CrtMemDifference(&stateDiff, &stateOld, &stateNew))
        _CrtMemDumpAllObjectsSince(&stateDiff);
    return 0;
}


