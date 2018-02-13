#pragma once

#include "IMedia.h"

class D3D9SurfaceVideoPlayer : public IVideoPlayer
{
public:
    D3D9SurfaceVideoPlayer();
    ~D3D9SurfaceVideoPlayer();

    std::tuple<FpState, std::shared_ptr<Clock>> AddVideo(std::shared_ptr<IVideoBufferInputStream> stream);
    FpState SetMasterClock(std::shared_ptr<Clock> clock);
    FpState Start();
    FpState WaitForStop();

    int64_t WndProc(void * pWnd, uint32_t message, uint64_t wParam, int64_t lParam);

    double_t Fps() const { return _fps; }
    double_t Pts() const { return _playClock ? _playClock->pts.operator double_t() : 0; }
private:
    FpState resetDevice();
    void videoThread();

private:
    std::shared_ptr<IVideoBufferInputStream> _stream;
    std::shared_ptr<Clock> _playClock;
    std::shared_ptr<Clock> _masterClock;

    std::atomic<FpState> _state = FpStateOK;

    void * _hWnd;
    com_ptr<struct IDirect3D9Ex> _d3d9;
    com_ptr<struct IDirect3DDevice9Ex> _d3d9Device;
    com_ptr<struct IDirect3DSurface9> _d3d9Surface;

    std::atomic<double_t> _fps = 0;
    double_t _fpsDuration = 10;

    VideoFormat _format;

    std::thread _thPlay;
    std::mutex _mtx;
    std::atomic<int32_t> _flags = FpFlagNone;
};
