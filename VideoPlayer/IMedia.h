#pragma once

const int TIME_BASE_S = AV_TIME_BASE;
const int TIME_BASE_MS = AV_TIME_BASE / 1000;

class IAudioDecoder
{
    
};

class IVideoPlayer
{
public:
    virtual ~IVideoPlayer() = default;
    virtual int doCombine(byte * data, int width, int height, int strike, int & duration) = 0;
};

class IVideoRender
{
public:

};
