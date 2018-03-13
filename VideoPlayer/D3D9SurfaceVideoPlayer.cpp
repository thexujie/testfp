#include "stdafx.h"
#include <array>
#include "D3D9SurfaceVideoPlayer.h"

#include <Windows.h>
#include <d3d9.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys.h>
#include <avrt.h>
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dxva2.lib")

#include <initguid.h>
#include <dxva2api.h>

#include <libavcodec/dxva2.h>
#include <libavutil/hwcontext_dxva2.h>

enum D3DFormatEx
{
	D3DFormatExNV12 = MAKEFOURCC('N', 'V', '1', '2'),
	D3DFormatExYV12 = MAKEFOURCC('Y', 'V', '1', '2'),
	D3DFormatExP010 = MAKEFOURCC('P', '0', '1', '0'),
	D3DFormatExIYUV = MAKEFOURCC('I', 'Y', 'U', 'V'),
};


DEFINE_GUID(DXVADDI_Intel_ModeH264_E, 0x604F8E68, 0x4951, 0x4C54, 0x88, 0xFE, 0xAB, 0xD2, 0x5C, 0x15, 0xB3, 0xD6);
DEFINE_GUID(DXVA2_ModeVP9_VLD_Profile0, 0x463707f8, 0xa1d0, 0x4585, 0x87, 0x6d, 0x83, 0xaa, 0x6d, 0x60, 0xb8, 0x9e);
DEFINE_GUID(DXVA2_ModeMJPEG_VLD_Intel, 0x91CD2D6E, 0x897B, 0x4FA1, 0xB0, 0xD7, 0x51, 0xDC, 0x88, 0x01, 0x0E, 0x0A);

AVCodecID MapDXVA2CodecID(const GUID & guid)
{
    static struct GUID2AVCodecID
    {
        const GUID * guid;
        enum AVCodecID codecId;
    }
    MAPS[] =
    {
        /* MPEG-2 */
        { &DXVA2_ModeMPEG1_VLD, AV_CODEC_ID_MPEG2VIDEO },
		{ &DXVA2_ModeMPEG2_VLD, AV_CODEC_ID_MPEG2VIDEO },
		{ &DXVA2_ModeMPEG2and1_VLD, AV_CODEC_ID_MPEG2VIDEO },

		/* H.264 */
		{ &DXVA2_ModeH264_A, AV_CODEC_ID_H264 },
		{ &DXVA2_ModeH264_B, AV_CODEC_ID_H264 },
		{ &DXVA2_ModeH264_C, AV_CODEC_ID_H264 },
		{ &DXVA2_ModeH264_D, AV_CODEC_ID_H264 },
		{ &DXVA2_ModeH264_E, AV_CODEC_ID_H264 },
		{ &DXVA2_ModeH264_F, AV_CODEC_ID_H264 },
		{ &DXVA2_ModeH264_VLD_WithFMOASO_NoFGT, AV_CODEC_ID_H264 },
		{ &DXVA2_ModeH264_VLD_Stereo_Progressive_NoFGT, AV_CODEC_ID_H264 },
		{ &DXVA2_ModeH264_VLD_Stereo_NoFGT, AV_CODEC_ID_H264 },
		{ &DXVA2_ModeH264_VLD_Multiview_NoFGT, AV_CODEC_ID_H264 },
		/* Intel specific H.264 mode */
		{ &DXVADDI_Intel_ModeH264_E, AV_CODEC_ID_H264 },

		/* VC-1 / WMV3 */
		{ &DXVA2_ModeVC1_D2010, AV_CODEC_ID_VC1 },
		{ &DXVA2_ModeVC1_D2010, AV_CODEC_ID_WMV3 },
		{ &DXVA2_ModeVC1_D, AV_CODEC_ID_VC1 },
		{ &DXVA2_ModeVC1_D, AV_CODEC_ID_WMV3 },

		/* HEVC/H.265 */
		{ &DXVA2_ModeHEVC_VLD_Main, AV_CODEC_ID_HEVC },
		{ &DXVA2_ModeHEVC_VLD_Main10, AV_CODEC_ID_HEVC },

		/* VP8/9 */
		{ &DXVA2_ModeVP9_VLD_Profile0, AV_CODEC_ID_VP9 },

		/* MJPEG */
		{ &DXVA2_ModeMJPEG_VLD_Intel, AV_CODEC_ID_MJPEG },
    };

    for each(const GUID2AVCodecID & item in MAPS)
    {
        if (*item.guid == guid)
            return item.codecId;
    }

    return AV_CODEC_ID_NONE;
}


static const CodecLevel CODEC_LEVELS[] = { CodecLevelSD, CodecLevelHD, CodecLevelFHD, CodecLevel4K, CodecLevel8K };
static const int32_t CODEC_WIDTHS[] = { 720, 1280, 1920, 3840, 7680 };
static const int32_t CODEC_HEIGHTS[] = { 480, 720, 1082, 2160, 4320 };

CodecLevel DXVA2CodecLevelFromSize(int32_t width, int32_t height)
{
    int32_t codecLevelW = -1;
    for(int32_t ilevel = 0; ilevel < std::size(CODEC_LEVELS); ++ilevel)
    {
        if (CODEC_WIDTHS[ilevel] > width)
            break;
        codecLevelW = ilevel;
    }

    if(codecLevelW >= 0)
    {
        int32_t codecLevelH = -1;
        for (int32_t ilevel = codecLevelW; ilevel <= std::size(CODEC_LEVELS); ++ilevel)
        {
            if (CODEC_HEIGHTS[ilevel] > height)
                break;
            codecLevelH = ilevel;
        }

        if (codecLevelH >= 0)
            return CODEC_LEVELS[std::max(codecLevelW, codecLevelH)];
    }

    return CodecLevelNone;
}

AVPixelFormat D3DFORMAT2AvPixelFormat(D3DFORMAT d3dformat)
{
    switch (d3dformat)
    {
    case D3DFMT_X8R8G8B8: return AV_PIX_FMT_0RGB;
    case D3DFMT_A8R8G8B8: return AV_PIX_FMT_ARGB;
    case (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2'): return AV_PIX_FMT_NV12;
	case (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2'): return AV_PIX_FMT_YUV420P;
	case (D3DFORMAT)MAKEFOURCC('Y', 'U', 'Y', '2'): return AV_PIX_FMT_YUYV422;
	case (D3DFORMAT)MAKEFOURCC('U', 'Y', 'V', 'Y'): return AV_PIX_FMT_UYVY422;
	case (D3DFORMAT)MAKEFOURCC('4', '1', '1', 'P'): return AV_PIX_FMT_YUV411P;
	case (D3DFORMAT)MAKEFOURCC('4', '4', '4', 'P'): return AV_PIX_FMT_YUVA444P;
	case (D3DFORMAT)MAKEFOURCC('P', '0', '1', '0'): return AV_PIX_FMT_YUV420P10;
	case (D3DFORMAT)MAKEFOURCC('R', 'G', 'B', 'P'): return AV_PIX_FMT_RGB24;
	case (D3DFORMAT)MAKEFOURCC('B', 'G', 'R', 'P'): return AV_PIX_FMT_BGR24;
    default: return AV_PIX_FMT_NONE;
    }
}

D3DFORMAT AvPixelFormat2D3DFORMAT(AVPixelFormat pixelFormat)
{
    switch (pixelFormat)
    {
    case AV_PIX_FMT_0RGB: return D3DFMT_X8R8G8B8;
    case AV_PIX_FMT_ARGB: return D3DFMT_A8R8G8B8;
    case AV_PIX_FMT_NV12: return (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
    case AV_PIX_FMT_YUV420P: return (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2');
    case AV_PIX_FMT_YUV420P10: return (D3DFORMAT)MAKEFOURCC('P', '0', '1', '0');
	case AV_PIX_FMT_DXVA2_VLD: return (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
    default: return D3DFMT_UNKNOWN;
    }
}
static u8string GUID2String(GUID guid) 
{
	std::array<char, 40> output;
	snprintf(output.data(), output.size(), "{%08X-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X}", 
		guid.Data1, guid.Data2, guid.Data3, 
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
	return u8string(output.data());
}

D3D9SurfaceVideoPlayerDefaultDevice::D3D9SurfaceVideoPlayerDefaultDevice(std::shared_ptr<D3D9Factory> d3d9, std::shared_ptr<IVideoRenderWindow> window) :_d3d9(d3d9), _window(window)
{
}

D3D9SurfaceVideoPlayerDefaultDevice::~D3D9SurfaceVideoPlayerDefaultDevice()
{

}

D3D9DeviceDesc D3D9SurfaceVideoPlayerDefaultDevice::GetDesc() const
{
    if (!_d3d9)
        return {};

    const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->initDevice();

    if (!_d3d9Device)
        return {};

    HRESULT hr = S_OK;
    com_ptr<struct IDirect3D9> d3d9 = _d3d9->GetD3D9();

    D3DDEVICE_CREATION_PARAMETERS createParam = {};
    hr = _d3d9Device->GetCreationParameters(&createParam);
    if (FAILED(hr))
        return {};

    D3DDISPLAYMODE displayMode = {};
    hr = _d3d9Device->GetDisplayMode(0, &displayMode);
    if (FAILED(hr))
        return {};

    D3DADAPTER_IDENTIFIER9 adapterIdentifier = {};
    hr = d3d9->GetAdapterIdentifier(createParam.AdapterOrdinal, 0, &adapterIdentifier);
    if (FAILED(hr))
        return {};

    com_ptr<IDirect3DSurface9> backBuffer0;
    hr = _d3d9Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer0);
    if (FAILED(hr))
        return {};

    D3DCAPS9 caps = {};
    hr = d3d9->GetDeviceCaps(createParam.AdapterOrdinal, D3DDEVTYPE_HAL, &caps);
    if (FAILED(hr))
        return {};

    D3DSURFACE_DESC backBufferDesc = {};
    backBuffer0->GetDesc(&backBufferDesc);

    D3D9DeviceDesc desc = {};

    desc.adapter = createParam.AdapterOrdinal;
    desc.deviceId = adapterIdentifier.DeviceId;
    desc.deviceIdentifier = adapterIdentifier.DeviceIdentifier;

    desc.driver = adapterIdentifier.Driver;
    desc.description = adapterIdentifier.Description;
    desc.displayDeviceName = adapterIdentifier.DeviceName;
    desc.driverVersion = adapterIdentifier.DriverVersion.QuadPart;
    desc.vendorId = adapterIdentifier.VendorId;
    desc.subSysId = adapterIdentifier.SubSysId;
    desc.revision = adapterIdentifier.Revision;
    desc.WHQLLevel = adapterIdentifier.WHQLLevel;

    desc.displayWidth = displayMode.Width;
    desc.displayHeight = displayMode.Height;

    desc.backBufferWidth = backBufferDesc.Width;
    desc.backBufferHeight = backBufferDesc.Height;

    return desc;
}

com_ptr<IDirect3DDevice9> D3D9SurfaceVideoPlayerDefaultDevice::GetD3D9Device() const
{
    const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->initDevice();
    return _d3d9Device;
}

CodecDeviceDesc D3D9SurfaceVideoPlayerDefaultDevice::GetCodecDeviceDesc() const
{
	D3D9DeviceDesc d3d9Desc = GetDesc();
	CodecDeviceDesc desc = {};
	desc.deviceIdentifier = GUID2String(d3d9Desc.deviceIdentifier);
	desc.deviceDescription = d3d9Desc.description;
	desc.vendorId = d3d9Desc.vendorId;
	desc.subSysId = d3d9Desc.subSysId;
	desc.subSysId = d3d9Desc.displayHeight;
	return desc;
}

VideoFormat D3D9SurfaceVideoPlayerDefaultDevice::GetDeviceVideoFormat() const
{
    const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->initDevice();
    if (!_d3d9Device)
        return {};

    com_ptr<IDirect3DSurface9> backBuffer0;
    HRESULT hr = _d3d9Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer0);
    if (FAILED(hr))
        return {};

    D3DSURFACE_DESC backBufferDesc = {};
    backBuffer0->GetDesc(&backBufferDesc);

    VideoFormat format;
    format.width = backBufferDesc.Width;
    format.height = backBufferDesc.Height;
    format.pixelFormat = D3DFORMAT2AvPixelFormat(backBufferDesc.Format);
    return format;
}

void * D3D9SurfaceVideoPlayerDefaultDevice::GetWindowHandle() const
{
    return _window ? _window->GetHandle() : nullptr;
}

FpState D3D9SurfaceVideoPlayerDefaultDevice::ResetDevice()
{
    HRESULT hr = S_OK;
    if (!_d3d9 || !_window)
        return FpStateBadState;

    _d3d9Device = nullptr;
    HWND hWnd = (HWND)_window->GetHandle();
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    int32_t width = std::max(rcClient.right - rcClient.left, 16L);
    int32_t height = std::max(rcClient.bottom - rcClient.top, 16L);

    uint32_t adapterId = D3DADAPTER_DEFAULT;

    com_ptr<struct IDirect3DDevice9> d3d9Device;
    D3DPRESENT_PARAMETERS d3dparam = {};
    d3dparam.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3dparam.BackBufferCount = 2;
    d3dparam.BackBufferFormat = D3DFMT_A8R8G8B8;
    d3dparam.BackBufferWidth = width;
    d3dparam.BackBufferHeight = height;
    d3dparam.EnableAutoDepthStencil = true;
    d3dparam.Flags = /*D3DPRESENTFLAG_*/0;
    d3dparam.FullScreen_RefreshRateInHz = 0;
    d3dparam.hDeviceWindow = hWnd;
    d3dparam.MultiSampleQuality = 0;
    d3dparam.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dparam.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dparam.SwapEffect = D3DSWAPEFFECT_FLIP;
    d3dparam.Windowed = TRUE;

    hr = _d3d9->GetD3D9()->CreateDevice(adapterId, D3DDEVTYPE_HAL, hWnd, /*D3DCREATE_HARDWARE_VERTEXPROCESSING*/0x00000020L | 0x00000004L | 0x00000002L, &d3dparam, &d3d9Device);
    if (FAILED(hr))
        return FpStateInner;

    _d3d9Device = d3d9Device;

    if(_d3d9DeviceManager)
    {
        hr = _d3d9DeviceManager->ResetDevice(_d3d9Device.get(), _resetToken);
        if (FAILED(hr))
            return FpStateInner;
		_sessonIndex.get()->operator++();
    }

    //TODO
    //resetDecoderService()

    return FpStateOK;
}

std::map<AVCodecID, std::vector<CodecDesc>> D3D9SurfaceVideoPlayerDefaultDevice::GetCodecDescs() const
{
	FpState state = const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->initDecoderService();
	if(state < 0 || !_decoderService)
		return {};

	com_ptr<IDirectXVideoDecoderService> decoderService = const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->_decoderService;

	HRESULT hr = S_OK;
	uint32_t numDecoderGuids = 0;
	GUID * decoderGuids = nullptr;
	hr = decoderService->GetDecoderDeviceGuids(&numDecoderGuids, &decoderGuids);
	if(FAILED(hr))
		return {};
	std::shared_ptr<GUID>(decoderGuids, [](GUID * guids) {CoTaskMemFree(guids); });

	std::map<AVCodecID, std::vector<CodecDesc>> descs;
	for(uint32_t idecoder = 0; idecoder < numDecoderGuids; ++idecoder)
	{
		const GUID & dxvaCodecGuid = decoderGuids[idecoder];
		AVCodecID dxvaCodecId = MapDXVA2CodecID(dxvaCodecGuid);

		uint32_t numDxvaOutputFormats = 0;
		D3DFORMAT * dxvaOutputFormats = nullptr;
		hr = decoderService->GetDecoderRenderTargets(dxvaCodecGuid, &numDxvaOutputFormats, &dxvaOutputFormats);
		if(FAILED(hr))
			continue;
		std::shared_ptr<D3DFORMAT> __dxvaOutputFormats(dxvaOutputFormats, [](D3DFORMAT * ptr) {CoTaskMemFree(ptr); });

		if(numDxvaOutputFormats < 1)
			continue;

		std::vector<AVPixelFormat> outputFormats;
		for(uint32_t iformat = 0; iformat < numDxvaOutputFormats; ++iformat)
			outputFormats.emplace_back(D3DFORMAT2AvPixelFormat(dxvaOutputFormats[iformat]));

		CodecLevel codecLevel = testCodecLevel(dxvaCodecGuid, AV_PIX_FMT_YUV420P);
		descs[dxvaCodecId].push_back({ GUID2String(dxvaCodecGuid), codecLevel, outputFormats });
	}
	return descs;
}

std::vector<CodecDesc> D3D9SurfaceVideoPlayerDefaultDevice::GetCodecDescs(AVCodecID codecId) const
{
	FpState state = const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->initDecoderService();
	if(state < 0 || !_decoderService)
		return {};


	com_ptr<IDirectXVideoDecoderService> decoderService = const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->_decoderService;
	HRESULT hr = S_OK;
	uint32_t numDecoderGuids = 0;
	GUID * decoderGuids = nullptr;
	hr = decoderService->GetDecoderDeviceGuids(&numDecoderGuids, &decoderGuids);
	if(FAILED(hr))
		return {};
	std::shared_ptr<GUID>(decoderGuids, [](GUID * guids) {CoTaskMemFree(guids); });

	std::vector<CodecDesc> descs;
	for(uint32_t idecoder = 0; idecoder < numDecoderGuids; ++idecoder)
	{
		const GUID & dxvaCodecGuid = decoderGuids[idecoder];
		AVCodecID dxvaCodecId = MapDXVA2CodecID(dxvaCodecGuid);
		if(dxvaCodecId != codecId)
			continue;

		uint32_t numDxvaOutputFormats = 0;
		D3DFORMAT * dxvaOutputFormats = nullptr;
		hr = decoderService->GetDecoderRenderTargets(dxvaCodecGuid, &numDxvaOutputFormats, &dxvaOutputFormats);
		if(FAILED(hr))
			continue;
		std::shared_ptr<D3DFORMAT>(dxvaOutputFormats, [](D3DFORMAT * ptr) {CoTaskMemFree(ptr); });

		if(numDxvaOutputFormats < 1)
			continue;

		std::vector<AVPixelFormat> outputFormats;
		for(uint32_t iformat = 0; iformat < numDxvaOutputFormats; ++iformat)
			outputFormats.emplace_back(D3DFORMAT2AvPixelFormat(dxvaOutputFormats[iformat]));

		CodecLevel codecLevel = testCodecLevel(dxvaCodecGuid, AV_PIX_FMT_YUV420P);
		descs.push_back({ GUID2String(dxvaCodecGuid), codecLevel, outputFormats });

		break;
	}
	return descs;
}

std::vector<CodecDesc> D3D9SurfaceVideoPlayerDefaultDevice::GetCodecDescs(VideoCodecFormat codecFormat) const
{
	FpState state = const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->initDecoderService();
	if(state < 0 || !_decoderService)
		return {};

	CodecLevel codecLevel = DXVA2CodecLevelFromSize(codecFormat.format.width, codecFormat.format.height);
	if(codecLevel == CodecLevelNone)
		return {};

	com_ptr<IDirectXVideoDecoderService> decoderService = const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->_decoderService;
	HRESULT hr = S_OK;
	uint32_t numDecoderGuids = 0;
	GUID * decoderGuids = nullptr;
	hr = decoderService->GetDecoderDeviceGuids(&numDecoderGuids, &decoderGuids);
	if(FAILED(hr))
		return {};
	std::shared_ptr<GUID>(decoderGuids, [](GUID * guids) {CoTaskMemFree(guids); });

	std::vector<CodecDesc> descs;
	for(uint32_t idecoder = 0; idecoder < numDecoderGuids; ++idecoder)
	{
		const GUID & dxvaCodecGuid = decoderGuids[idecoder];
		AVCodecID dxvaCodecId = MapDXVA2CodecID(dxvaCodecGuid);
		if(dxvaCodecId != codecFormat.codecId)
			continue;

		uint32_t numDxvaOutputFormats = 0;
		D3DFORMAT * dxvaOutputFormats = nullptr;
		hr = decoderService->GetDecoderRenderTargets(dxvaCodecGuid, &numDxvaOutputFormats, &dxvaOutputFormats);
		if(FAILED(hr))
			continue;
		std::shared_ptr<D3DFORMAT>(dxvaOutputFormats, [](D3DFORMAT * ptr) {CoTaskMemFree(ptr); });

		if(numDxvaOutputFormats < 1)
			continue;

		std::vector<AVPixelFormat> outputFormats;
		for(uint32_t iformat = 0; iformat < numDxvaOutputFormats; ++iformat)
			outputFormats.emplace_back(D3DFORMAT2AvPixelFormat(dxvaOutputFormats[iformat]));

		bool codecTest = testCodec(dxvaCodecGuid, codecFormat);
		if(!codecTest)
			continue;

		descs.push_back({ GUID2String(dxvaCodecGuid), codecLevel, outputFormats });

		break;
	}
	return descs;
}

std::tuple<AVHWDeviceType, std::vector<AVPixelFormat>> D3D9SurfaceVideoPlayerDefaultDevice::ChooseDevice(const std::vector<AVHWDeviceType> & hwDeviceTypes, VideoCodecFormat codecFormat) const
{
    AVHWDeviceType hwDeviceTypeUse = AV_HWDEVICE_TYPE_NONE;
    for(AVHWDeviceType hwDeviceType : hwDeviceTypes)
    {
        // dxva2
        if (hwDeviceType == AV_HWDEVICE_TYPE_DXVA2)
        {
            hwDeviceTypeUse = AV_HWDEVICE_TYPE_DXVA2;
            break;
        }
    }
    if (hwDeviceTypeUse == AV_HWDEVICE_TYPE_NONE)
        return { AV_HWDEVICE_TYPE_NONE, {} };

    std::vector<CodecDesc> dxva2CodecDescs = GetCodecDescs(codecFormat);
    return { dxva2CodecDescs.empty() ? AV_HWDEVICE_TYPE_NONE : AV_HWDEVICE_TYPE_DXVA2, dxva2CodecDescs[0].outputFormats };
}

struct D3D9SurfaceVideoPlayerDefaultDeviceHWAccelContext : public IVideoDecoderHWAccelContext, public std::enable_shared_from_this<IVideoDecoderHWAccelContext>
{
public:
    D3D9SurfaceVideoPlayerDefaultDeviceHWAccelContext(com_ptr<IDirect3DDeviceManager9> d3d9DeviceManager,
        std::shared_ptr<std::atomic<int32_t>> sessonIndex):_d3d9DeviceManager(d3d9DeviceManager), _sessonIndex(sessonIndex){}
    ~D3D9SurfaceVideoPlayerDefaultDeviceHWAccelContext()
    {
        for (int32_t cnt = 0; cnt < _dxvaContext.surface_count; ++cnt)
            _dxvaContext.surface[cnt]->Release();
        _decoderService.reset();
        if(_hDevice)
            _d3d9DeviceManager->CloseDeviceHandle((HANDLE)_hDevice.get());
    }

    FpState SetCodecFormat(VideoCodecFormat codecFormat)
    {
        HRESULT hr = S_OK;
        if(!_decoderService)
        {
            HANDLE hDevice = INVALID_HANDLE_VALUE;
            hr = _d3d9DeviceManager->OpenDeviceHandle(&hDevice);
            if (FAILED(hr))
                return FpStateInner;

            com_ptr<IDirectXVideoDecoderService> decoderService;
            hr = _d3d9DeviceManager->GetVideoService(hDevice, __uuidof(IDirectXVideoDecoderService), (void **)&decoderService);
            if (FAILED(hr))
            {
                _d3d9DeviceManager->CloseDeviceHandle(hDevice);
                return FpStateInner;
            }

            _hDevice.reset((void *)hDevice, [this](void * ptr)
            {
                if(ptr)
                    _d3d9DeviceManager->CloseDeviceHandle((HANDLE)ptr);
            });
            _decoderService = decoderService;
        }

        DXVA2_VideoDesc desc = { 0 };
        desc.SampleWidth = codecFormat.format.width;
        desc.SampleHeight = codecFormat.format.height;
        desc.Format = (D3DFORMAT)D3DFormatExNV12;

        GUID decoderGuid = {};
        DXVA2_ConfigPictureDecode dxvaConfig = {};

        uint32_t numDecoderGuids = 0;
        GUID * decoderGuids = nullptr;
        hr = _decoderService->GetDecoderDeviceGuids(&numDecoderGuids, &decoderGuids);
        if (FAILED(hr))
            return FpStateGeneric;

        std::shared_ptr<GUID>(decoderGuids, [](GUID * guids) {CoTaskMemFree(guids); });

        for (uint32_t idecoder = 0; idecoder < numDecoderGuids; ++idecoder)
        {
            const GUID & dxvaCodecGuid = decoderGuids[idecoder];
            AVCodecID guidCodecId = MapDXVA2CodecID(dxvaCodecGuid);
            if (guidCodecId != codecFormat.codecId)
                continue;

            // 测试是否支持指定的解码器
            uint32_t numConfigs = 0;
            DXVA2_ConfigPictureDecode * configs = nullptr;
            hr = _decoderService->GetDecoderConfigurations(dxvaCodecGuid, &desc, NULL, &numConfigs, &configs);
            if (FAILED(hr))
                continue;

            std::shared_ptr<DXVA2_ConfigPictureDecode>(configs, [](DXVA2_ConfigPictureDecode * configs) {CoTaskMemFree(configs); });

            uint32_t scoreMax = 0;
            uint32_t scoreMaxConfigIndex = 0;
            for (uint32_t icfg = 0; icfg < numConfigs; ++icfg)
            {
                const DXVA2_ConfigPictureDecode & cfg = configs[icfg];

                uint32_t score = 0;
                if (cfg.ConfigBitstreamRaw == 1)
                    score = 1;
                else if (codecFormat.codecId == AV_CODEC_ID_H264 && cfg.ConfigBitstreamRaw == 2)
                    score = 2;
                else
                    continue;

                if (IsEqualGUID(cfg.guidConfigBitstreamEncryption, DXVA2_NoEncrypt))
                    score += 16;

                if (score > scoreMax)
                {
                    scoreMax = score;
                    scoreMaxConfigIndex = icfg;
                }
            }

            if (scoreMax)
            {
                uint32_t numOutputFormats = 0;
                D3DFORMAT * outputFormats = nullptr;
                hr = _decoderService->GetDecoderRenderTargets(dxvaCodecGuid, &numOutputFormats, &outputFormats);
                if (FAILED(hr))
                    continue;
                std::shared_ptr<D3DFORMAT>(outputFormats, [](D3DFORMAT * guids) {CoTaskMemFree(guids); });

                if (numOutputFormats < 1)
                    continue;

                desc.Format = outputFormats[0];
                decoderGuid = dxvaCodecGuid;
                dxvaConfig = configs[scoreMaxConfigIndex];
                break;
            }
        }

        if (desc.Format == D3DFMT_UNKNOWN)
            return FpStateGeneric;

        int numSurfaces = 0;
        int surfaceAlignment = 0;
        /* decoding MPEG-2 requires additional alignment on some Intel GPUs,
        but it causes issues for H.264 on certain AMD GPUs..... */
        if (codecFormat.codecId == AV_CODEC_ID_MPEG2VIDEO)
            surfaceAlignment = 32;
        /* the HEVC DXVA2 spec asks for 128 pixel aligned surfaces to ensure
        all coding features have enough room to work with */
        else if (codecFormat.codecId == AV_CODEC_ID_HEVC)
            surfaceAlignment = 128;
        else
            surfaceAlignment = 16;

        /* 4 base work surfaces */
        numSurfaces = 4;

        /* add surfaces based on number of possible refs */
        if (codecFormat.codecId == AV_CODEC_ID_H264 || codecFormat.codecId == AV_CODEC_ID_HEVC)
            numSurfaces += 16;
        else if (codecFormat.codecId == AV_CODEC_ID_VP9)
            numSurfaces += 8;
        else
            numSurfaces += 2;

        /* add extra surfaces for frame threading */
        ////if (codecCtx->active_thread_type & FF_THREAD_FRAME)
        ////    ctx->num_surfaces += codecCtx->thread_count;

        D3DFORMAT format = (D3DFORMAT)D3DFormatExNV12;;
        IDirect3DSurface9 ** surfaces = (IDirect3DSurface9 **)av_mallocz(numSurfaces * sizeof(IDirect3DSurface9 *));

        hr = _decoderService->CreateSurface(
            FFALIGN(codecFormat.format.width, surfaceAlignment),
            FFALIGN(codecFormat.format.height, surfaceAlignment),
            numSurfaces - 1,
            format, D3DPOOL_DEFAULT, 0,
            DXVA2_VideoDecoderRenderTarget,
            surfaces, NULL);

        if (FAILED(hr))
            return FpStateInner;

        //desc.SampleWidth = codecFormat.format.width;
        //desc.SampleHeight = codecFormat.format.height;
        //desc.Format = AvPixelFormat2D3DFORMAT(codecFormat.format.pixelFormat);

        com_ptr<IDirectXVideoDecoder> decoder;
        hr = _decoderService->CreateVideoDecoder(decoderGuid,
            &desc, &dxvaConfig, surfaces,
            numSurfaces, &decoder);
        if (FAILED(hr))
            return FpStateInner;

        _inputCodecFormat = codecFormat;
        _decoderGuid = decoderGuid;
        _dxvaConfig = dxvaConfig;
        _decoder = decoder;

        _dxvaContext = {};
        _dxvaContext.cfg = &_dxvaConfig;
        _dxvaContext.decoder = _decoder.get();
        _dxvaContext.surface = surfaces;
        _dxvaContext.surface_count = numSurfaces;
        if (_decoderGuid == DXVADDI_Intel_ModeH264_E)
            _dxvaContext.workaround |= FF_DXVA2_WORKAROUND_INTEL_CLEARVIDEO;

        _numSurfaces = numSurfaces;
        _surfaces.reset(surfaces, [numSurfaces](IDirect3DSurface9 ** surfaces)
        {
            if (!surfaces)
                return;

            for(int32_t cnt = 0; cnt < numSurfaces; ++cnt)
            {
                if (surfaces[cnt])
                    surfaces[cnt]->Release();
            }
            av_free(surfaces);
        });
        used.resize(numSurfaces, false);
        used.assign(numSurfaces, false);

        _sessonIndexCurr = *_sessonIndex.get();
        return FpStateOK;
    }

    FpState NeedReset() const
    {
        if (!_hDevice)
            return FpStateBadState;

        HRESULT hr = _d3d9DeviceManager->TestDevice((HANDLE)_hDevice.get());
        if (hr == DXVA2_E_NEW_VIDEO_DEVICE)
        {
			//return *_sessonIndex.get() == _sessonIndexCurr ? FpStateBadState : FpStateOK;
			return FpStateOK;
        }
        else
            return FpStateBadState;
    }

    FpState Reset()
    {
        printf("Reset dxva2 decoder sesson = %d.\n", _sessonIndex.get()->operator int());
        std::lock_guard<std::mutex> lock(mtx);
        _surfaces.reset();
        _numSurfaces = 0;
        _decoder.reset();
        _decoderService.reset();
        _hDevice.reset();
        //return FpStateOK;
        return SetCodecFormat(_inputCodecFormat);
    }

    virtual void * GetFFmpegHWAccelContext()
    {
        return &_dxvaContext;
    }

    void * GetFFmpegHWDeviceContext()
    {
        return &_dxva2DeviceContext;
    }

    AVPixelFormat GetOutputPixelFormat() const
    {
        return AV_PIX_FMT_DXVA2_VLD;
    }

    FpState ReleaseBuffer(IDirect3DSurface9 * surface)
    {
        if (!surface)
            return FpStateNullptr;

        std::lock_guard<std::mutex> lock(mtx);
        if(_dxvaContext.surface)
        {
            for (int32_t cnt = 0; cnt < _dxvaContext.surface_count; ++cnt)
            {
                if (_dxvaContext.surface[cnt] == surface)
                {
                    used[cnt] = false;
                    break;
                }
            }
        }
        surface->Release();
        return FpStateOK;
    }

    static void dxva2_release_buffer(void * opaque, uint8_t *data)
    {
        ((D3D9SurfaceVideoPlayerDefaultDeviceHWAccelContext *)(opaque))->ReleaseBuffer((IDirect3DSurface9 *)data);
    }

    static void dxva2_framebuffer_free(void * opaque, uint8_t *data)
    {
        AVFrameDXVAContext * ctx = (AVFrameDXVAContext *)data;
        delete ctx;
    }

    FpState GetBuffer(std::shared_ptr<AVFrame> frame, int32_t flags)
    {
        std::lock_guard<std::mutex> lock(mtx);

        int32_t index = -1;
        for (uint32_t cnt = 0; cnt < _numSurfaces; cnt++)
        {
            if(!used[cnt])
            {
                index = cnt;
                break;
            }
        }
        if (index == -1)
        {
            assert(false);
            return FpStateNoData;
        }

        IDirect3DSurface9 * surface = _surfaces.get()[index];
        surface->AddRef();
        frame->buf[0] = av_buffer_create((uint8_t*)surface, 0, dxva2_release_buffer, (void *)this, AV_BUFFER_FLAG_READONLY);

        AVFrameDXVAContext * ctx = new AVFrameDXVAContext();
		ctx->_acceleratorContext = shared_from_this();
		ctx->_d3d9DeviceManager = _d3d9DeviceManager;
        ctx->_hDevice = _hDevice;
        ctx->_decoderService = _decoderService;
        ctx->_decoder = _decoder;
        ctx->_surfaces = _surfaces;
        frame->buf[1] = av_buffer_create((uint8_t *)ctx, 0, dxva2_framebuffer_free, nullptr, AV_BUFFER_FLAG_READONLY);

        if (!frame->buf[0])
        {
            assert(false);
            return FpStateNoData;
        }

        frame->data[3] = (uint8_t *)surface;
        used[index] = true;

        return FpStateOK;
    }

    struct AVFrameDXVAContext
    {
    public:
        std::mutex _mtx;
        int32_t refCount = 1;
		std::shared_ptr<IVideoDecoderHWAccelContext> _acceleratorContext;
        com_ptr<IDirect3DDeviceManager9> _d3d9DeviceManager;
        std::shared_ptr<void> _hDevice;
        com_ptr<IDirectXVideoDecoderService> _decoderService;
        com_ptr<IDirectXVideoDecoder> _decoder;
        std::shared_ptr<IDirect3DSurface9 *> _surfaces;
    };

    std::mutex mtx;
    int32_t _sessonIndexCurr = 0;
    com_ptr<IDirect3DDeviceManager9> _d3d9DeviceManager;
	int32_t _resetSessionIndex = 0;
    std::shared_ptr<std::atomic<int32_t>> _sessonIndex;

    std::shared_ptr<void> _hDevice;
    com_ptr<IDirectXVideoDecoderService> _decoderService;

    GUID _decoderGuid = {};
    DXVA2_ConfigPictureDecode _dxvaConfig = {};
    com_ptr<IDirectXVideoDecoder> _decoder;

    int32_t _numSurfaces = 0;
    std::shared_ptr<IDirect3DSurface9 *> _surfaces;

    std::vector<bool> used;

    VideoCodecFormat _inputCodecFormat;
    VideoFormat _outputCodecFormat;

    dxva_context _dxvaContext = {};
    AVDXVA2DeviceContext _dxva2DeviceContext = {};
};

std::tuple<AVHWDeviceType, std::shared_ptr<IVideoDecoderHWAccelContext>> D3D9SurfaceVideoPlayerDefaultDevice::CreateAccelerator(const std::vector<AVHWDeviceType> & hwDeviceTypes, VideoCodecFormat codecFormat)
{
    AVHWDeviceType hwDeviceTypeUse = AV_HWDEVICE_TYPE_NONE;
    for (AVHWDeviceType hwDeviceType : hwDeviceTypes)
    {
        // dxva2
        if (hwDeviceType == AV_HWDEVICE_TYPE_DXVA2)
        {
            hwDeviceTypeUse = AV_HWDEVICE_TYPE_DXVA2;
            break;
        }
    }
    if (hwDeviceTypeUse == AV_HWDEVICE_TYPE_NONE)
        return { AV_HWDEVICE_TYPE_NONE,  nullptr };

    HRESULT hr = S_OK;
    // dxva2
    FpState state = initDecoderService();
    if (state < 0 || !_decoderService)
        return { AV_HWDEVICE_TYPE_NONE, nullptr };


    std::shared_ptr<D3D9SurfaceVideoPlayerDefaultDeviceHWAccelContext> ctx = std::make_shared<D3D9SurfaceVideoPlayerDefaultDeviceHWAccelContext>(_d3d9DeviceManager, _sessonIndex);
    state = ctx->SetCodecFormat(codecFormat);
    if(state < 0)
        return { AV_HWDEVICE_TYPE_NONE, nullptr };
    return { AV_HWDEVICE_TYPE_DXVA2, ctx };
}

FpState D3D9SurfaceVideoPlayerDefaultDevice::initDevice()
{
    if (_d3d9Device)
        return FpStateOK;

    HRESULT hr = S_OK;
    if (!_d3d9 || !_window)
        return FpStateBadState;

    HWND hWnd = (HWND)_window->GetHandle();
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    int32_t width = std::max(rcClient.right - rcClient.left, 16L);
    int32_t height = std::max(rcClient.bottom - rcClient.top, 16L);

    uint32_t adapterId = D3DADAPTER_DEFAULT;

    com_ptr<struct IDirect3DDevice9> d3d9Device;
    D3DPRESENT_PARAMETERS d3dparam = {};
    d3dparam.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3dparam.BackBufferCount = 2;
    d3dparam.BackBufferFormat = D3DFMT_A8R8G8B8;
    d3dparam.BackBufferWidth = width;
    d3dparam.BackBufferHeight = height;
    d3dparam.EnableAutoDepthStencil = true;
    d3dparam.Flags = /*D3DPRESENTFLAG_*/0;
    d3dparam.FullScreen_RefreshRateInHz = 0;
    d3dparam.hDeviceWindow = hWnd;
    d3dparam.MultiSampleQuality = 0;
    d3dparam.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dparam.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dparam.SwapEffect = D3DSWAPEFFECT_FLIP;
    d3dparam.Windowed = TRUE;

    hr = _d3d9->GetD3D9()->CreateDevice(adapterId, D3DDEVTYPE_HAL, hWnd, /*D3DCREATE_HARDWARE_VERTEXPROCESSING*/0x00000020L | 0x00000004L | 0x00000002L, &d3dparam, &d3d9Device);
    if (FAILED(hr))
        return FpStateInner;

    _d3d9Device = d3d9Device;

    //TODO
    //resetDecoderService()

    return FpStateOK;
}

FpState D3D9SurfaceVideoPlayerDefaultDevice::initDecoderService()
{
    if (_decoderService)
        return FpStateOK;

    if (!_d3d9Device && ResetDevice() < 0)
        return FpStateInner;

    HRESULT hr = S_OK;
    uint32_t resetToken = 0;
    com_ptr<IDirect3DDeviceManager9> d3d9DeviceManager;
    hr = DXVA2CreateDirect3DDeviceManager9(&resetToken, &d3d9DeviceManager);
    if (FAILED(hr))
        return FpStateInner;

    hr = d3d9DeviceManager->ResetDevice(_d3d9Device.get(), resetToken);
    if (FAILED(hr))
        return FpStateInner;

    HANDLE handle = INVALID_HANDLE_VALUE;
    hr = d3d9DeviceManager->OpenDeviceHandle(&handle);
    if (FAILED(hr))
        return FpStateInner;

    com_ptr<IDirectXVideoDecoderService> decoderService;
    hr = d3d9DeviceManager->GetVideoService(handle, __uuidof(IDirectXVideoDecoderService), (void **)&decoderService);
    if (FAILED(hr))
        return FpStateInner;

    _hDevice = (void *)handle;
    _resetToken = resetToken;
    _d3d9DeviceManager = d3d9DeviceManager;
    _decoderService = decoderService;
    _sessonIndex = std::make_shared<std::atomic<int32_t>>(1);
    return FpStateOK;
}

bool D3D9SurfaceVideoPlayerDefaultDevice::testCodec(const GUID & dxvaCodecGuid, VideoCodecFormat format) const
{
    FpState state = const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->initDecoderService();
    if (state < 0 || !_decoderService)
        return false;

    HRESULT hr = S_OK;
    DXVA2_VideoDesc desc = { 0 };
    desc.SampleWidth = format.format.width;
    desc.SampleHeight = format.format.height;
    desc.Format = AvPixelFormat2D3DFORMAT(format.format.pixelFormat);

    uint32_t numDxvaConfigs = 0;
    DXVA2_ConfigPictureDecode * dxvaConfigs = nullptr;
    hr = _decoderService->GetDecoderConfigurations(dxvaCodecGuid, &desc, NULL, &numDxvaConfigs, &dxvaConfigs);
    if (FAILED(hr))
        return false;

    std::shared_ptr<DXVA2_ConfigPictureDecode>(dxvaConfigs, [](DXVA2_ConfigPictureDecode * ptr) {CoTaskMemFree(ptr); });
    if (numDxvaConfigs < 1)
        return false;

    uint32_t scoreMax = 0;
    for (uint32_t icfg = 0; icfg < numDxvaConfigs; ++icfg)
    {
        const DXVA2_ConfigPictureDecode & cfg = dxvaConfigs[icfg];

        uint32_t score = 0;
        if (cfg.ConfigBitstreamRaw == 1)
            score = 1;
        else if (format.codecId == AV_CODEC_ID_H264 && cfg.ConfigBitstreamRaw == 2)
            score = 2;
        else
            continue;

        if (IsEqualGUID(cfg.guidConfigBitstreamEncryption, DXVA2_NoEncrypt))
            score += 16;

        if (score > scoreMax)
        {
            return true;
        }
    }
    return false;
}

CodecLevel D3D9SurfaceVideoPlayerDefaultDevice::testCodecLevel(const GUID & dxvaCodecGuid, AVPixelFormat pixelFormat) const
{
    FpState state = const_cast<D3D9SurfaceVideoPlayerDefaultDevice *>(this)->initDecoderService();
    if (state < 0 || !_decoderService)
        return CodecLevelNone;

    HRESULT hr = S_OK;
    uint32_t numDecoderGuids = 0;
    GUID * decoderGuids = nullptr;
    hr = _decoderService->GetDecoderDeviceGuids(&numDecoderGuids, &decoderGuids);
    if (FAILED(hr))
        return CodecLevelNone;

    DXVA2_VideoDesc desc = { 0 };
    desc.Format = AvPixelFormat2D3DFORMAT(pixelFormat);

    CodecLevel codecLevel = CodecLevelNone;
    for(int32_t level = 0; level < std::size(CODEC_WIDTHS); ++level)
    {
        uint32_t numDxvaOutputFormats = 0;
        D3DFORMAT * dxvaOutputFormats = nullptr;
        hr = _decoderService->GetDecoderRenderTargets(dxvaCodecGuid, &numDxvaOutputFormats, &dxvaOutputFormats);
        if (FAILED(hr))
            continue;
        std::shared_ptr<D3DFORMAT>(dxvaOutputFormats, [](D3DFORMAT * ptr) {CoTaskMemFree(ptr); });
        if (numDxvaOutputFormats < 1)
            continue;

        desc.SampleWidth = CODEC_WIDTHS[level];
        desc.SampleHeight = CODEC_HEIGHTS[level];

        uint32_t numDxvaConfigs= 0;
        DXVA2_ConfigPictureDecode * dxvaConfigs = nullptr;
        hr = _decoderService->GetDecoderConfigurations(dxvaCodecGuid, &desc, NULL, &numDxvaConfigs, &dxvaConfigs);
        if (FAILED(hr))
            continue;
        std::shared_ptr<DXVA2_ConfigPictureDecode>(dxvaConfigs, [](DXVA2_ConfigPictureDecode * ptr) {CoTaskMemFree(ptr); });
        if (numDxvaConfigs < 1)
            break;

        codecLevel = CODEC_LEVELS[level];
    }
    return codecLevel;
}

D3D9SurfaceVideoPlayer::D3D9SurfaceVideoPlayer(std::shared_ptr<ID3D9SurfaceVideoPlayerDevice> d3d9Device) :_device(d3d9Device)
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
    _clock = std::make_shared<Clock>();
    return { FpStateOK, _clock };
}

FpState D3D9SurfaceVideoPlayer::SetMasterClock(std::shared_ptr<Clock> clock)
{
    std::lock_guard<std::mutex> lock(_mtx);
    _masterClock = clock;
    return FpStateOK;
}

FpState D3D9SurfaceVideoPlayer::Start()
{
    if (_thPlay.get_id() == std::thread::id())
        _thPlay = std::thread(std::bind(&D3D9SurfaceVideoPlayer::videoThread, this));
    return FpStateOK;
}

FpState D3D9SurfaceVideoPlayer::Stop()
{
    if (_thPlay.joinable())
    {
        _flags = _flags | FpFlagStop;
        _thPlay.join();
    }
    return FpStateOK;
}

FpState D3D9SurfaceVideoPlayer::WaitForStop()
{
    if (_thPlay.get_id() == std::thread::id())
        return FpStateOK;

    _thPlay.join();
    return FpStateOK;
}


void D3D9SurfaceVideoPlayer::videoThread()
{
    thread_set_name(0, "D3D9SurfaceVideoPlayer::videoThread");
    printf("video play start.\n");

    if (!_device)
    {
        _state = FpStateBadState;
        return;
    }

    //DWORD taskIndex = 0;
    //HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
    //::SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    HRESULT hr = S_OK;
    double_t tsBase = get_time_hr();
    double_t tsLastFrame = 0;
    double_t tsNextFrame = 0;
    double_t tsLastPresent = 0;
    double_t ptsPlay = 0;
    int64_t numFrames = 0;
    double_t tsFpsLast = 0;

    VideoBuffer buffer = {};

	bool keepLastContent = false;
	double_t tsResetLast = get_time_hr();
    while (_state >= 0 && !(_flags & FpFlagStop))
    {
		keepLastContent = false;
        HWND hWnd = (HWND)_device->GetWindowHandle();
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int32_t width = std::max(rcClient.right - rcClient.left, 16L);
        int32_t height = std::max(rcClient.bottom - rcClient.top, 16L);
        com_ptr<IDirect3DDevice9> renderDevice = _device->GetD3D9Device();
        VideoFormat deviceFormat = _device->GetDeviceVideoFormat();
        //检查是否要重置设备
        if (!renderDevice || 
			((width != deviceFormat.width || height != deviceFormat.height) && get_time_hr() - tsResetLast > 0.5))
        {
            _d3d9Surface.reset();
            _device->ResetDevice();
            renderDevice = _device->GetD3D9Device();
            if (!renderDevice)
            {
                Sleep(10);
                continue;
            }
        }
        //取下一帧
        while (!buffer.avframe && _state >= 0)
        {
            _state = _stream->PeekBuffer(buffer);
            if (_state == FpStatePending)
            {
                Sleep(1);
                continue;
            }
            assert(_state >= 0 || _state == FpStateEOF);
        }

        if (_state < 0 || _state == FpStatePending)
            break;

        assert(buffer.avframe);
        if (!buffer.avframe)
        {
            _state = FpStateNoData;
            break;
        }

		D3DFORMAT d3dfmtOfBuffer = D3DFMT_UNKNOWN;
		switch(buffer.avframe->format)
		{
		case AV_PIX_FMT_NV12:
			d3dfmtOfBuffer = (D3DFORMAT)D3DFormatExNV12;
			break;
		case AV_PIX_FMT_YUV420P:
			d3dfmtOfBuffer = (D3DFORMAT)D3DFormatExIYUV;
			break;
		case AV_PIX_FMT_DXVA2_VLD:
			d3dfmtOfBuffer = (D3DFORMAT)D3DFormatExNV12;
			break;
		default:
			d3dfmtOfBuffer = D3DFMT_UNKNOWN;
			break;
		}

		//创建 Surface
		if(_d3d9Surface)
		{
			D3DSURFACE_DESC surfaceDesc = {};
			_d3d9Surface->GetDesc(&surfaceDesc);
			if(surfaceDesc.Width != buffer.width || surfaceDesc.Height != buffer.height ||
				d3dfmtOfBuffer != surfaceDesc.Format)
				_d3d9Surface.reset();
		}


		if(!_d3d9Surface && d3dfmtOfBuffer != D3DFMT_UNKNOWN)
		{
			hr = renderDevice->CreateOffscreenPlainSurface(
				buffer.width, buffer.height,
				d3dfmtOfBuffer,
				D3DPOOL_DEFAULT,
				&_d3d9Surface,
				NULL);
			if(FAILED(hr))
			{
				assert(false);
				_state = FpStateInner;
				break;
			}
		}

        // 传输内容
        com_ptr<struct IDirect3DSurface9> surface;
        D3DSURFACE_DESC dstSurfaceDesc = {};
        _d3d9Surface->GetDesc(&dstSurfaceDesc);
        if (buffer.avframe->format == AV_PIX_FMT_DXVA2_VLD)
        {
            com_ptr<IDirect3DSurface9> dataSurface(reinterpret_cast<IDirect3DSurface9 *>(buffer.avframe->data[3]));
            if (dataSurface)
                dataSurface->AddRef();

            com_ptr<IDirect3DDevice9> dataSurfaceDevice;
            hr = dataSurface->GetDevice(&dataSurfaceDevice);
            if (SUCCEEDED(hr) && dataSurfaceDevice == renderDevice)
            {
                surface = dataSurface;
            }
        }

        D3DLOCKED_RECT dstLockedRect = {};
        if(!surface)
        {
            hr = _d3d9Surface->LockRect(&dstLockedRect, NULL, D3DLOCK_DISCARD | D3DLOCK_DONOTWAIT);
            if (FAILED(hr))
            {
                _state = FpStateInner;
                continue;
            }
        }

        if (surface)
        {
            // 使用 StretchRect
        }
        else if (buffer.avframe->format == AV_PIX_FMT_NV12)
        {
            uint8_t * bitsY = (uint8_t *)dstLockedRect.pBits;
            for (int32_t row = 0; row < buffer.height; ++row)
            {
                uint8_t * dst = bitsY + dstLockedRect.Pitch * row;
                uint8_t * src = buffer.avframe->data[0] + buffer.avframe->linesize[0] * row;
                memcpy(dst, src, buffer.avframe->linesize[0]);
            }

            uint8_t * dstUV = (uint8_t *)dstLockedRect.pBits + dstLockedRect.Pitch * dstSurfaceDesc.Height;
            uint8_t * srcUV = buffer.avframe->data[1];
            for (int32_t row = 0; row < buffer.height / 2; ++row)
            {
                uint8_t * dst = dstUV + dstLockedRect.Pitch * row;
                uint8_t * src = srcUV + buffer.avframe->linesize[1] * row;
                memcpy(dst, src, std::min(dstLockedRect.Pitch, buffer.avframe->linesize[1]));
            }

        }
        else if (buffer.avframe->format == AV_PIX_FMT_YUV420P)
        {
            uint8_t * bitsY = (uint8_t *)dstLockedRect.pBits;
            for (int32_t row = 0; row < buffer.height; ++row)
            {
                uint8_t * dst = bitsY + dstLockedRect.Pitch * row;
                uint8_t * src = buffer.avframe->data[0] + buffer.avframe->linesize[0] * row;
                memcpy(dst, src, buffer.avframe->linesize[0]);
            }

            uint8_t * bitsU = bitsY + dstLockedRect.Pitch * dstSurfaceDesc.Height;
            uint8_t * bitsV = bitsU + dstLockedRect.Pitch * dstSurfaceDesc.Height / 4;
            for (int32_t hrow = 0; hrow < buffer.height / 2; ++hrow)
            {
                uint8_t * dstU = bitsU + dstLockedRect.Pitch / 2 * hrow;
                uint8_t * srcU = buffer.avframe->data[1] + buffer.avframe->linesize[1] * hrow;
                //memset(dstU, 0x80, buffer.width / 2);
                memcpy(dstU, srcU, buffer.avframe->linesize[1]);

                uint8_t * dstV = bitsV + dstLockedRect.Pitch / 2 * hrow;
                uint8_t * srcV = buffer.avframe->data[2] + buffer.avframe->linesize[2] * hrow;
                //memset(dstV, 0x80, buffer.width / 2);
                memcpy(dstV, srcV, buffer.avframe->linesize[2]);
            }
        }
        else if (buffer.avframe->format == AV_PIX_FMT_DXVA2_VLD)
        {
			keepLastContent = true;
            IDirect3DSurface9 * dataSurface = reinterpret_cast<IDirect3DSurface9 *>(buffer.avframe->data[3]);
            assert(dataSurface);
            if(dataSurface)
            {
    //            dataSurface->AddRef();

    //            D3DSURFACE_DESC srcSurfaceDesc = {};
    //            dataSurface->GetDesc(&srcSurfaceDesc);

				//// https://msdn.microsoft.com/zh-cn/library/windows/desktop/bb174471(v=vs.85).aspx
				//RECT rect = { 0, 0, buffer.width, buffer.height };
				//POINT point = {};
				////不能使用 StretchRect
				////hr = _d3d9Device->StretchRect(dataSurface, &rect, _d3d9Surface.get(), &rect, D3DTEXF_NONE);

				//if(srcSurfaceDesc.Format == D3DFormatExNV12)
				//{
				//	D3DLOCKED_RECT srcLockedRect;
				//	hr = dataSurface->LockRect(&srcLockedRect, NULL, D3DLOCK_READONLY);
				//	if(SUCCEEDED(hr))
				//	{
				//		uint8_t * dstY = (uint8_t *)dstLockedRect.pBits;
				//		uint8_t * srcY = (uint8_t *)srcLockedRect.pBits;
				//		for(int32_t row = 0; row < buffer.height; ++row)
				//		{
				//			uint8_t * dst = dstY + dstLockedRect.Pitch * row;
				//			uint8_t * src = srcY + srcLockedRect.Pitch * row;
				//			memcpy(dst, src, std::min(srcLockedRect.Pitch, dstLockedRect.Pitch));
				//		}

				//		uint8_t * dstUV = (uint8_t *)dstLockedRect.pBits + dstLockedRect.Pitch * dstSurfaceDesc.Height;
				//		// surfaceDesc.Height >= buffer.height
				//		uint8_t * srcUV = (uint8_t *)srcLockedRect.pBits + srcLockedRect.Pitch * srcSurfaceDesc.Height;
				//		for(int32_t row = 0; row < buffer.height / 2; ++row)
				//		{
				//			uint8_t * dst = dstUV + dstLockedRect.Pitch * row;
				//			//memset(dst, 0x80, dstLockedRect.Pitch);
				//			uint8_t * src = srcUV + srcLockedRect.Pitch * row;
				//			memcpy(dst, src, std::min(dstLockedRect.Pitch, srcLockedRect.Pitch));
				//		}
				//		hr = dataSurface->UnlockRect();
				//		assert(SUCCEEDED(hr));
				//	}
				//}
            }
        }
        else
        {
            _state = FpStateInner;
            assert(false);
            break;
        }

        if(dstLockedRect.pBits)
        {
            hr = _d3d9Surface->UnlockRect();
            if (FAILED(hr))
            {
                assert(false);
                _state = FpStateInner;
                break;
            }
        }
		if(!keepLastContent)
		{
			renderDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
			hr = renderDevice->BeginScene();
			if(FAILED(hr))
			{
				assert(false);
				_state = FpStateInner;
				break;
			}

			com_ptr<IDirect3DSurface9> backBuffer;
			hr = renderDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
			if(FAILED(hr))
			{
				assert(false);
				_state = FpStateInner;
				break;
			}

			D3DSURFACE_DESC backBufferDesc = {};
			backBuffer->GetDesc(&backBufferDesc);

			RECT rcSrc = { 0, 0, buffer.width, buffer.height };
			RECT rcDst = { 0, 0, backBufferDesc.Width, backBufferDesc.Height };
			double_t ratio = static_cast<double_t>(buffer.width) / buffer.height;
			double_t nWidth = backBufferDesc.Height * ratio;
			double_t nHeight = backBufferDesc.Width / ratio;
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

			hr = renderDevice->StretchRect(surface ? surface.get() : _d3d9Surface.get(), &rcSrc, backBuffer.get(), &rcDst, D3DTEXF_LINEAR);
			renderDevice->EndScene();
		}
        else
        {
            hr = renderDevice->BeginScene();
            if (FAILED(hr))
            {
                assert(false);
                _state = FpStateInner;
                break;
            }
            renderDevice->EndScene();
        }
        //surface 可能还在引用，需要 StretchRect 之后再释放 AVFrame。
        tsNextFrame = buffer.pts;
        buffer = {};

        //已经准备好，计算等待时间
        // ---------------------------------------
        double_t tsNow = get_time_hr();
        // 计算帧间隔
        ++numFrames;
        ptsPlay = tsNow - tsBase;
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

        double_t delay = tsNextFrame - tsLastFrame -(get_time_hr() - tsLastPresent);
        // 同步到主时钟
        if (_masterClock)
        {
            double_t diff = _clock->pts - _masterClock->pts;
            double sync_threshold = std::max(_syncThresholdMin, std::min(_syncThresholdMax, delay));
            if (!isnan(diff)/* && fabs(diff) < buffer.duration*/)
            {
                if (diff > sync_threshold)
                    delay = 2 * delay;
                else if (diff < sync_threshold && delay > 0.1)
                    delay = delay + diff;
                else if (diff < -sync_threshold)
                    delay = std::max(0.0, delay + diff);
                else
                    ;
            }
        }

        int64_t sleepTime = delay * 1000;
        if (sleepTime > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
        //----------------------------------------

        hr = renderDevice->Present(NULL, NULL, NULL, NULL);
        _clock->pts = tsNextFrame;
        tsLastFrame = tsNextFrame;
        tsLastPresent = get_time_hr();

        auto [bufferCount, bufferCountMax] = _stream->BufferQuality();
        //// 输出信息
        printf("\r[%02d:%02d:%02d.%03d] fps=%.2lf, A-V=%.2lf, quality=%lld/%lld.",
            (int32_t)_clock->pts / 3600,
            (int32_t)_clock->pts % 3600 / 60,
            (int32_t)_clock->pts % 60,
            (int32_t)(_clock->pts * 1000) % 1000,
            _fps.operator double_t(),
            _masterClock ? _clock->pts - _masterClock->pts : 0,
            bufferCount, bufferCountMax);

        if(hr == D3DERR_DEVICELOST)
        {
            _d3d9Surface.reset();
            _device->ResetDevice();
        }
    }

    printf("video play end.\n");
}
