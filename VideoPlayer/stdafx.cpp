#include "stdafx.h"
#include "IMedia.h"
#include <windows.h>

#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "Winmm.lib")


FILE * g_logFile = nullptr;
FILE * logFile()
{
    return g_logFile;
}


u8string cs2u8(const char * cbs, int32_t length)
{
    int32_t nu16 = MultiByteToWideChar(CP_ACP, 0, cbs, length, NULL, 0);
    wchar_t * u16 = new wchar_t[nu16];
    MultiByteToWideChar(CP_ACP, 0, cbs, length, u16, nu16);

    int32_t nu8 = WideCharToMultiByte(CP_UTF8, 0, u16, nu16, NULL, 0, NULL, FALSE);
    delete[] u16;
    char * u8 = new char[nu8];
    WideCharToMultiByte(CP_UTF8, 0, u16, nu16, u8, nu8, NULL, FALSE);
    u8string str(u8);
    delete[] u8;
    return str;

}

u8string ws2u8(const wchar_t * wcbs, int32_t length)
{
    return ws2cs(wcbs, length);

    int32_t nu8 = WideCharToMultiByte(CP_UTF8, 0, wcbs, length, NULL, 0, NULL, FALSE);
    char * u8 = new char[nu8];
    WideCharToMultiByte(CP_UTF8, 0, wcbs, length, u8, nu8, NULL, FALSE);
    u8string str(u8);
    delete[] u8;
    return str;
}

u8string ws2cs(const wchar_t * wcbs, int32_t length)
{
    int32_t nu8 = WideCharToMultiByte(CP_ACP, 0, wcbs, length, NULL, 0, NULL, FALSE);
    char * u8 = new char[nu8];
    WideCharToMultiByte(CP_ACP, 0, wcbs, length, u8, nu8, NULL, FALSE);
    u8string str(u8);
    delete[] u8;
    return str;
}
