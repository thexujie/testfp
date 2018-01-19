#pragma once
#include "IMedia.h"
#include "CSObject.h"
#include <d3d9.h>

class XuVideoRenderD3D9
{
public:
    XuVideoRenderD3D9();
    ~XuVideoRenderD3D9();

    int init(IVideoPlayer * player);

    void start();

    int doRender();

    LRESULT ThisWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
private:
    static unsigned __stdcall renderThread(void * args);
private:
    HWND m_hWnd;
    IDirect3D9Ex * m_d3d9;
    IDirect3DDevice9Ex * m_d3dDevice9Ex;
    IDirect3DSurface9 * m_d3dSurface;

    IVideoPlayer * m_player;

    HANDLE m_hPlayThread;
    long m_endPlayThread;
    std::vector<std::string> m_playList;

    CSObject m_csPlayList;
    CSObject m_csPlayMix;
    long long m_dwLastResize;
};
