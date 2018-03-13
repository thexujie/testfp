#pragma once

#include "IMedia.h"

class VideoRenderWindow : public IVideoRenderWindow
{
public:
    VideoRenderWindow();
    ~VideoRenderWindow();

    void * GetHandle() const { return _hWnd; }

    int64_t WndProc(void * pWnd, uint32_t message, uint64_t wParam, int64_t lParam);
private:
    void * _hWnd;
};

