#include "stdafx.h"
#include "VideoRender.h"
#pragma comment(lib, "d3d9.lib")
#include <DirectXMath.h>
#include <process.h>
using namespace DirectX;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

XuVideoRenderD3D9 * g_this = NULL;
XuVideoRenderD3D9::XuVideoRenderD3D9()
{
    g_this = this;
}

XuVideoRenderD3D9::~XuVideoRenderD3D9()
{

}

int XuVideoRenderD3D9::init(IVideoPlayer * player)
{
    m_player = player;
    HINSTANCE hInstance = GetModuleHandle(NULL);
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &m_d3d9);

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
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

    int width = 1280;
    int height = 720;
    m_hWnd = CreateWindowExW(0, L"XuVideoRenderD3D9", L"AABB", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
    ShowWindow(m_hWnd, SW_SHOW);


    D3DPRESENT_PARAMETERS d3dparam = {};
    d3dparam.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3dparam.BackBufferCount = 2;
    d3dparam.BackBufferFormat = D3DFMT_A8R8G8B8;
    d3dparam.BackBufferHeight = height;
    d3dparam.BackBufferWidth = width;
    d3dparam.EnableAutoDepthStencil = true;
    d3dparam.Flags = /*D3DPRESENTFLAG_*/0;
    d3dparam.FullScreen_RefreshRateInHz = 0;
    d3dparam.hDeviceWindow = m_hWnd;
    d3dparam.MultiSampleQuality = 0;
    d3dparam.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dparam.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dparam.SwapEffect = D3DSWAPEFFECT_FLIP;
    d3dparam.Windowed = TRUE;
    hr = m_d3d9->CreateDeviceEx(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dparam, NULL, &m_d3dDevice9Ex);

    hr = m_d3dDevice9Ex->CreateOffscreenPlainSurface(
        width, height,
        D3DFMT_X8R8G8B8,
        D3DPOOL_DEFAULT,
        &m_d3dSurface,
        NULL);

    return hr;
}

void XuVideoRenderD3D9::start()
{
    if(!m_hPlayThread)
        m_hPlayThread = (HANDLE)_beginthreadex(NULL, 0, renderThread, (void *)this, 0, NULL);
}

int XuVideoRenderD3D9::doRender()
{
    while(m_d3dSurface)
    {
        int64_t t_start = av_gettime_relative();
        if(m_dwLastResize > 0 && GetTickCount64() - m_dwLastResize > 500)
        {
            RECT rcClient;
            GetClientRect(m_hWnd, &rcClient);
            if(rcClient.right > rcClient.left)
            {
                rcClient.right -= rcClient.left;
                rcClient.bottom -= rcClient.top;
                rcClient.left = rcClient.top = 0;

                m_d3dSurface->Release();
                m_d3dDevice9Ex->Release();
                D3DPRESENT_PARAMETERS d3dparam = {};
                d3dparam.AutoDepthStencilFormat = D3DFMT_D24S8;
                d3dparam.BackBufferCount = 2;
                d3dparam.BackBufferFormat = D3DFMT_A8R8G8B8;
                d3dparam.BackBufferHeight = rcClient.bottom;
                d3dparam.BackBufferWidth = rcClient.right;
                d3dparam.EnableAutoDepthStencil = true;
                d3dparam.Flags = /*D3DPRESENTFLAG_*/0;
                d3dparam.FullScreen_RefreshRateInHz = 0;
                d3dparam.hDeviceWindow = m_hWnd;
                d3dparam.MultiSampleQuality = 0;
                d3dparam.MultiSampleType = D3DMULTISAMPLE_NONE;
                d3dparam.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
                d3dparam.SwapEffect = D3DSWAPEFFECT_FLIP;
                d3dparam.Windowed = TRUE;
                HRESULT hr = m_d3d9->CreateDeviceEx(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dparam, NULL, &m_d3dDevice9Ex);

                hr = m_d3dDevice9Ex->CreateOffscreenPlainSurface(
                    1280, 720,
                    D3DFMT_X8R8G8B8,
                    D3DPOOL_DEFAULT,
                    &m_d3dSurface,
                    NULL);

                InterlockedExchange64(&m_dwLastResize, 0);
            }
            else
                InterlockedExchange64(&m_dwLastResize, GetTickCount64());
        }

        D3DSURFACE_DESC cavansrDesc = {};
        m_d3dSurface->GetDesc(&cavansrDesc);
        D3DLOCKED_RECT d3d_rect;
        HRESULT hr = m_d3dSurface->LockRect(&d3d_rect, NULL, D3DLOCK_DONOTWAIT);
        if(FAILED(hr))
        {
            Sleep(10);
            continue;
        }

        int duration = 0;
        m_player->doCombine((char *)d3d_rect.pBits, cavansrDesc.Width, cavansrDesc.Height, d3d_rect.Pitch, duration);

        hr = m_d3dSurface->UnlockRect();
        if(FAILED(hr))
            return -1;

        if(m_d3dDevice9Ex == NULL)
            return -1;

        m_d3dDevice9Ex->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        m_d3dDevice9Ex->BeginScene();
        IDirect3DSurface9 * pBackBuffer = NULL;

        m_d3dDevice9Ex->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
        D3DSURFACE_DESC backBufferDesc = {};
        pBackBuffer->GetDesc(&backBufferDesc);

        RECT rcSrc = { 0, 0, cavansrDesc.Width, cavansrDesc.Height };
        RECT rcDst = { 0, 0, backBufferDesc.Width, backBufferDesc.Height };
        double ratio = (double)cavansrDesc.Width / cavansrDesc.Height;
        double nWidth = backBufferDesc.Height * ratio;
        double nHeight = backBufferDesc.Width / ratio;
        if(nWidth > backBufferDesc.Width)
        {
            rcDst.top = (backBufferDesc.Height - nHeight) / 2;
            rcDst.bottom = rcDst.top + nHeight;
        }
        else
        {
            rcDst.left = (backBufferDesc.Width - nWidth) / 2;
            rcDst.right = rcDst.left + nWidth;
        }
    
        m_d3dDevice9Ex->StretchRect(m_d3dSurface, &rcSrc, pBackBuffer, &rcDst, D3DTEXF_LINEAR);
        m_d3dDevice9Ex->EndScene();
        m_d3dDevice9Ex->Present(NULL, NULL, NULL, NULL);

        int64_t t_sleep = duration / TIME_BASE_MS - (av_gettime_relative() - t_start)/ TIME_BASE_MS;
        if(t_sleep < 1)
            t_sleep = 1;
        Sleep(t_sleep);
    }
}

LRESULT XuVideoRenderD3D9::ThisWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{

    switch(message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
    case WM_ERASEBKGND:
        return 0;
    break;
    case WM_GETMINMAXINFO:
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        InterlockedExchange64(&m_dwLastResize, GetTickCount64());
        break;

        //case WM_NCCALCSIZE:
        //    return 1;
        //{
        //    int xFrame = 0; /*左右边框的厚度*/
        //    int yFrame = 0; /*下边框的厚度*/
        //    int nTHight = 0; /*标题栏的高度*/
        //    NCCALCSIZE_PARAMS * p = (NCCALCSIZE_PARAMS *)(lParam);
        //    if(wParam)
        //    {
        //        RECT rcDst = p->rgrc[0];
        //        RECT rcSrc = p->rgrc[1];

        //        p->rgrc[0] = rcDst;
        //        p->rgrc[1] = rcDst;
        //        p->rgrc[2] = rcSrc;
        //    }
        //    else
        //    {
        //        RECT * rc = (RECT *)lParam;

        //        rc->left = rc->left + xFrame;
        //        rc->top = rc->top + nTHight;
        //        rc->right = rc->right - xFrame;
        //        rc->bottom = rc->bottom - yFrame;
        //    }
        //    return 0;
        //}
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}


unsigned XuVideoRenderD3D9::renderThread(void * args)
{
    XuVideoRenderD3D9 * pThis = (XuVideoRenderD3D9 *)args;
    pThis->doRender();
    return 0;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return g_this->ThisWndProc(hWnd, message, wParam, lParam);;
}