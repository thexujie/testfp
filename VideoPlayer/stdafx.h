// stdafx.h : 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 特定于项目的包含文件
//

#pragma once


#define NOMINMAX

#include <SDKDDKVer.h>

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <tchar.h>

#include <string>
#include <vector>
#include <assert.h>

#include <iostream>
#include <map>
#include <list>
#include <memory>
#include <map>
#include <queue>
#include <functional>
#include <mutex>
#include <atomic>

#include <guiddef.h>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
#include <libavutil/imgutils.h>
#include "libavutil/avutil.h"
#include <libavutil/opt.h>
#include "libavutil/time.h"
}

#include "com_ptr.h"

enum FpState
{
    FpStateOK = 0,
    //-------------------------- errors
    FpStateEOF = -0x7fffffff,
    FpStateGeneric,
    FpStateInner,
    FpStateNullptr,
    FpStateOutOfMemory,
    FpStateInvalidParams,
    FpStateTimeOut,

    FpStateBadState,
    FpStateBadData,

    FpStateNotReady,
    FpStateNotNow,
    FpStateNotSupported,
    FpStateNotInitialized,

    FpStateNoData,

    //-------------------------- states
    FpStatePending = 1,
    FpStateAlready,
};

enum FpFlag
{
    FpFlagNone = 0,
    FpFlagStop = 0x0001,
};

typedef std::string a8string;
typedef std::string u8string;
typedef std::wstring u16string;


#include "IMedia.h"

FILE * logFile();

#ifdef AP_LOG
#define log(...) fprintf(logFile(),...)
#else
#define log(...)
#endif

u8string cs2u8(const char * cbs, int32_t length);
u8string ws2u8(const wchar_t * wcbs, int32_t length);
u8string ws2cs(const wchar_t * wcbs, int32_t length);

void thread_set_name(int thread_id, const char * name);
void thread_prom();
void print_averrr(int averr);

inline double_t get_time_hr()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto nowmcs = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    return nowmcs / 1000000.0;
}
