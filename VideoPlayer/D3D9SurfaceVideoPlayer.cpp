#include "stdafx.h"
#include "D3D9SurfaceVideoPlayer.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d9.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys.h>
#include <avrt.h>
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "d3d9.lib")

const wchar_t WINDOW_PROP_THIS_PTR[] = L"C8C8BD2D-46A7-4DFB-BB5D-EE6A25E83368";

D3D9SurfaceVideoPlayer::D3D9SurfaceVideoPlayer()
{
}

D3D9SurfaceVideoPlayer::~D3D9SurfaceVideoPlayer()
{
    if (_thPlay.joinable())
    {
        _flags = _flags | FpFlagStop;
        _thPlay.join();
    }
}

std::tuple<FpState, std::shared_ptr<Clock>> D3D9SurfaceVideoPlayer::AddVideo(std::shared_ptr<IVideoBufferInputStream> stream)
{
    std::lock_guard<std::mutex> lock(_mtx);
    _stream = stream;
    _playClock = std::make_shared<Clock>();
    return { FpStateOK, _playClock };
}

FpState D3D9SurfaceVideoPlayer::SetMasterClock(std::shared_ptr<Clock> clock)
{
    _masterClock = clock;
    return FpStateOK;
}

static LRESULT CALLBACK VideoPlayerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    D3D9SurfaceVideoPlayer * player = static_cast<D3D9SurfaceVideoPlayer *>(GetPropW(hWnd, WINDOW_PROP_THIS_PTR));
    if (player)
        return player->WndProc(reinterpret_cast<void *>(hWnd), message, wParam, lParam);
    else
        return CallWindowProcW(DefWindowProcW, hWnd, message, wParam, lParam);
}

FpState D3D9SurfaceVideoPlayer::Start()
{
    HINSTANCE hInstance = GetModuleHandle(NULL);

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = VideoPlayerWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = NULL;
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"XuVideoRenderD3D9";
    wcex.hIconSm = NULL;

    RegisterClassExW(&wcex);

    _hWnd = CreateWindowExW(0, L"XuVideoRenderD3D9", L"AABB", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
    ShowWindow((HWND)_hWnd, SW_SHOW);

    SetPropW((HWND)_hWnd, WINDOW_PROP_THIS_PTR, (void *)(D3D9SurfaceVideoPlayer *)this);
    if (_thPlay.joinable())
        return FpStateOK;

    _thPlay = std::thread(std::bind(&D3D9SurfaceVideoPlayer::videoThread, this));
    return FpStateOK;
}

FpState D3D9SurfaceVideoPlayer::WaitForStop()
{
    if (_thPlay.get_id() == std::thread::id())
        return FpStateOK;

    _thPlay.join();
    return FpStateOK;
}

FpState D3D9SurfaceVideoPlayer::resetDevice()
{
    HRESULT hr = S_OK;
    if (!_d3d9)
    {
        hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &_d3d9);
        if (FAILED(hr))
        {
            _state = FpStateInner;
            return FpStateInner;
        }
    }

    com_ptr<struct IDirect3DDevice9Ex> d3d9Device;
    com_ptr<struct IDirect3DSurface9> d3d9Surface;
    D3DPRESENT_PARAMETERS d3dparam = {};
    d3dparam.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3dparam.BackBufferCount = 2;
    d3dparam.BackBufferFormat = D3DFMT_A8R8G8B8;
    d3dparam.BackBufferWidth = _format.width;
    d3dparam.BackBufferHeight = _format.height;
    d3dparam.EnableAutoDepthStencil = true;
    d3dparam.Flags = /*D3DPRESENTFLAG_*/0;
    d3dparam.FullScreen_RefreshRateInHz = 0;
    d3dparam.hDeviceWindow = (HWND)_hWnd;
    d3dparam.MultiSampleQuality = 0;
    d3dparam.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dparam.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dparam.SwapEffect = D3DSWAPEFFECT_FLIP;
    d3dparam.Windowed = TRUE;
    hr = _d3d9->CreateDeviceEx(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dparam, NULL, &d3d9Device);
    if (FAILED(hr))
    {
        _state = FpStateInner;
        return FpStateInner;
    }

    _d3d9Device = d3d9Device;

    return FpStateOK;
}
#include <iomanip>
void D3D9SurfaceVideoPlayer::videoThread()
{
    RECT rcClient;
    GetClientRect((HWND)_hWnd, &rcClient);
    _format.width = rcClient.right - rcClient.left;
    _format.height = rcClient.bottom - rcClient.top;
    _format.pixelFormat = AV_PIX_FMT_BGRA;

    if (!_d3d9Device)
        resetDevice();

    //DWORD taskIndex = 0;
    //HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
    //::SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    HRESULT hr = S_OK;
    double_t tsBase = get_time_hr();
    double_t ptsPlay = 0;
    int64_t numFrames = 0;
    double_t tsFpsLast = 0;

    while (_state >= 0 && !(_flags & FpFlagStop))
    {
        double_t tsFrameA = get_time_hr();
        GetClientRect((HWND)_hWnd, &rcClient);
        int32_t width = rcClient.right - rcClient.left;
        int32_t height = rcClient.bottom - rcClient.top;
        if (!_d3d9Device || width != _format.width || height != _format.height)
        {
            _format.width = width;
            _format.height = height;
            _d3d9Surface.reset();
            _d3d9Device.reset();
            resetDevice();
        }

        VideoBuffer buffer = {};
        _state = _stream->PeekBuffer(buffer);
        if (_state == FpStatePending)
        {
            Sleep(10);
            continue;
        }
        if (!buffer.data)
        {
            _state = FpStateNoData;
            break;
        }

        if (_d3d9Surface)
        {
            D3DSURFACE_DESC surfaceDesc = {};
            _d3d9Surface->GetDesc(&surfaceDesc);
            if (surfaceDesc.Width != buffer.width || surfaceDesc.Height != buffer.height)
                _d3d9Surface.reset();
        }
        if (!_d3d9Surface)
        {
            hr = _d3d9Device->CreateOffscreenPlainSurface(
                buffer.width, buffer.height,
                D3DFMT_X8R8G8B8,
                D3DPOOL_DEFAULT,
                &_d3d9Surface,
                NULL);
            if (FAILED(hr))
            {
                _state = FpStateInner;
                break;
            }
        }

        D3DLOCKED_RECT d3d_rect;
        hr = _d3d9Surface->LockRect(&d3d_rect, NULL, D3DLOCK_DONOTWAIT);
        if (FAILED(hr))
        {
            Sleep(10);
            continue;
        }

        uint8_t * dst = (uint8_t *)d3d_rect.pBits;
        for (int32_t row = 0; row < buffer.height; ++row)
        {
            memcpy(dst + buffer.pitch * row, buffer.data.get() + buffer.pitch * row, buffer.pitch);
        }

        hr = _d3d9Surface->UnlockRect();
        if (FAILED(hr))
        {
            _state = FpStateInner;
            break;
        }

        _d3d9Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        hr = _d3d9Device->BeginScene();
        if (FAILED(hr))
        {
            _state = FpStateInner;
            break;
        }

        com_ptr<IDirect3DSurface9> backBuffer;
        hr = _d3d9Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
        if (FAILED(hr))
        {
            _state = FpStateInner;
            break;
        }

        D3DSURFACE_DESC backBufferDesc = {};
        backBuffer->GetDesc(&backBufferDesc);

        RECT rcSrc = { 0, 0, buffer.width, buffer.height };
        RECT rcDst = { 0, 0, backBufferDesc.Width, backBufferDesc.Height };
        double_t ratio = (double_t)buffer.width / buffer.height;
        double_t nWidth = backBufferDesc.Height * ratio;
        double_t nHeight = backBufferDesc.Width / ratio;
        if (nWidth > backBufferDesc.Width)
        {
            rcDst.top = (backBufferDesc.Height - nHeight) / 2;
            rcDst.bottom = rcDst.top + nHeight;
        }
        else
        {
            rcDst.left = (backBufferDesc.Width - nWidth) / 2;
            rcDst.right = rcDst.left + nWidth;
        }

        _d3d9Device->StretchRect(_d3d9Surface.get(), &rcSrc, backBuffer.get(), &rcDst, D3DTEXF_LINEAR);
        _d3d9Device->EndScene();
        _d3d9Device->Present(NULL, NULL, NULL, NULL);

        printf("\r[%02d:%02d:%02d.%03d:]fps=%.2lf",
            (int32_t)_playClock->pts / 3600,
            (int32_t)_playClock->pts % 3600 / 60,
            (int32_t)_playClock->pts % 60,
            (int32_t)(_playClock->pts * 1000) % 1000,
            _fps.operator double_t());

        ++numFrames;
        ptsPlay = get_time_hr() - tsBase;
        if (ptsPlay - tsFpsLast >= _fpsDuration)
        {
            tsFpsLast = ptsPlay;
            _fps = numFrames / _fpsDuration;
            numFrames = 0;
        }
        else
        {
            if (tsFpsLast < 0.1)
                _fps = numFrames / (ptsPlay - tsFpsLast);
        }

        double_t tsFrame = get_time_hr() - tsFrameA;
        double_t delay = buffer.duration - tsFrame;
        if (_masterClock)
        {
            double_t diff = _playClock->pts - _masterClock->pts;
            double sync_threshold = std::max(0.04, std::min(0.1, delay));
            if (!isnan(diff)/* && fabs(diff) < buffer.duration*/)
            {
                if (diff > sync_threshold)
                    delay = 2 * delay;
                else if (diff < sync_threshold && delay > 0.1)
                    delay = delay + diff;
                else if (diff < -sync_threshold)
                    delay = std::max(0.0, delay + diff);
                else
                    delay = delay;
            }
        }
        _playClock->pts = _playClock->pts + buffer.duration;
        int64_t slt = delay * 1000;
        if (slt > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(slt));

    }
}

int64_t D3D9SurfaceVideoPlayer::WndProc(void * pWnd, uint32_t message, uint64_t wParam, int64_t lParam)
{
    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 0;
    default:
        break;
    }
    return CallWindowProcW(DefWindowProcW, (HWND)pWnd, message, wParam, lParam);
}