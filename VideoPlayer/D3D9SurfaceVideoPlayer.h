#pragma once

#include "IMedia.h"
#include "D3D9Factory.h"

struct IDirect3DDeviceManager9;
struct IDirectXVideoDecoderService;
struct IDirectXVideoDecoder;
class ID3D9SurfaceVideoPlayerDevice
{
public:
    virtual ~ID3D9SurfaceVideoPlayerDevice() = default;
    virtual com_ptr<IDirect3DDevice9> GetD3D9Device() const = 0;
    virtual VideoFormat GetDeviceVideoFormat() const = 0;
    virtual void * GetWindowHandle() const = 0;
    virtual FpState ResetDevice() = 0;
};

class D3D9SurfaceVideoPlayerDefaultDevice : public ID3D9SurfaceVideoPlayerDevice, public IVideoDecoderHWAccelerator
{
public:
    D3D9SurfaceVideoPlayerDefaultDevice(std::shared_ptr<D3D9Factory> d3d9, std::shared_ptr<IVideoRenderWindow> window);
    ~D3D9SurfaceVideoPlayerDefaultDevice();

    D3D9DeviceDesc GetDesc() const;
    com_ptr<IDirect3DDevice9> GetD3D9Device() const;
    VideoFormat GetDeviceVideoFormat() const;
    void * GetWindowHandle() const;

    FpState ResetDevice();

	CodecDeviceDesc GetCodecDeviceDesc() const;
	std::map<AVCodecID, std::vector<CodecDesc>> GetCodecDescs() const;
	std::vector<CodecDesc> GetCodecDescs(AVCodecID codecId) const;
	std::vector<CodecDesc> GetCodecDescs(VideoCodecFormat codecFormat) const;
    std::tuple<AVHWDeviceType, std::vector<AVPixelFormat>> ChooseDevice(const std::vector<AVHWDeviceType> & hwDeviceTypes, VideoCodecFormat codecFormat) const;
    std::tuple<AVHWDeviceType, std::shared_ptr<IVideoDecoderHWAccelContext>> CreateAccelerator(const std::vector<AVHWDeviceType> & hwDeviceTypes, VideoCodecFormat codecFormat);

private:
    FpState initDevice();
    FpState initDecoderService();
    bool testCodec(const GUID & dxvaCodecGuid, VideoCodecFormat format) const;
    CodecLevel testCodecLevel(const GUID & dxvaCodecGuid, AVPixelFormat pixelFormat) const;

private:
    std::shared_ptr<D3D9Factory> _d3d9;
    std::shared_ptr<IVideoRenderWindow> _window;
    com_ptr<IDirect3DDevice9> _d3d9Device;

    //decoder
    uint32_t _resetToken = 0;
    com_ptr<IDirect3DDeviceManager9> _d3d9DeviceManager;
    std::shared_ptr<std::atomic<int32_t>> _sessonIndex;
    com_ptr<IDirectXVideoDecoderService> _decoderService;
    void * _hDevice = NULL;
};

class D3D9SurfaceVideoPlayer : public IVideoPlayer
{
public:
    D3D9SurfaceVideoPlayer(std::shared_ptr<ID3D9SurfaceVideoPlayerDevice> d3d9Device);
    ~D3D9SurfaceVideoPlayer();

    std::tuple<FpState, std::shared_ptr<Clock>> AddVideo(std::shared_ptr<IVideoBufferInputStream> stream);
    FpState SetMasterClock(std::shared_ptr<Clock> clock);
    FpState Start();
    FpState Stop();
    FpState WaitForStop();

    double_t Fps() const { return _fps; }
    double_t Pts() const { return _clock ? _clock->pts.operator double_t() : 0; }

private:
    void videoThread();

private:
    std::shared_ptr<IVideoBufferInputStream> _stream;
    std::shared_ptr<Clock> _clock;
    std::shared_ptr<Clock> _masterClock;

    std::atomic<FpState> _state = FpStateOK;

    std::shared_ptr<ID3D9SurfaceVideoPlayerDevice> _device;
    com_ptr<struct IDirect3DSurface9> _d3d9Surface;

    std::atomic<double_t> _fps = 0;
    double_t _fpsDuration = 3;

    std::thread _thPlay;
    std::mutex _mtx;
    std::atomic<int32_t> _flags = FpFlagNone;
    double_t _syncThresholdMin = 0.04;
    double_t _syncThresholdMax = 0.1;
};
