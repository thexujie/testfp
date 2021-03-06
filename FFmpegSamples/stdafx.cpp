#include "stdafx.h"

#pragma comment(lib, "sdl2.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avfilter.lib")
#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "postproc.lib")
#pragma comment(lib, "swresample.lib")

int decode_audio_main(int argc, char **argv);
extern "C"
{
int ffplayer_main(int argc, char **argv);
}

int main(int argc, char **argv)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    return ffplayer_main(argc, argv);
}

extern "C"
{
    void thread_set_name(int thread_id, const char * name)
    {
        if(!thread_id)
            thread_id = GetCurrentThreadId();
#pragma pack(push, 8)
        struct THREADNAME_INFO
        {
            uint32_t dwType; // must be 0x1000
            const char * name; // pointer to name (in same addr space)
            int thread_id; // thread ID (-1 caller thread)
            uint32_t flags; // reserved for future use, most be zero
        };
#pragma pack(pop)

        const uint32_t MS_VC_EXCEPTION_SET_THREAD_NAME = 0x406d1388;
        THREADNAME_INFO info;
        info.dwType = 0x1000;
        info.name = name;
        info.thread_id = thread_id;
        info.flags = 0;
        __try
        {
            RaiseException(MS_VC_EXCEPTION_SET_THREAD_NAME, 0, sizeof(THREADNAME_INFO) / sizeof(ULONG_PTR), (ULONG_PTR *)&info);
        }
        __except(EXCEPTION_CONTINUE_EXECUTION)
        {
        }
    }

}