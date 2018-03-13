#pragma once

struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DSurface9;

struct D3D9DeviceDesc
{
    uint32_t adapter = 0;
    uint32_t deviceId = 0;
    GUID deviceIdentifier = {};

    u8string driver;
    u8string description;
    //d3d9 设备必须连接在一个显示设备上
    u8string displayDeviceName;
    int64_t driverVersion = 0;
    uint32_t vendorId = 0;
    uint32_t subSysId = 0;
    uint32_t revision = 0;
    uint32_t WHQLLevel = 0;

    int32_t displayWidth = 0;
    int32_t displayHeight = 0;

    int32_t backBufferWidth = 0;
    int32_t backBufferHeight = 0;
};

class D3D9Factory
{
public:
    D3D9Factory();
    ~D3D9Factory();

    std::vector<D3D9DeviceDesc> GetDeviceDescs() const;

    com_ptr<IDirect3D9> GetD3D9() const;
private:
    FpState loadModule();
private:
    com_ptr<IDirect3D9> _d3d9;
};
