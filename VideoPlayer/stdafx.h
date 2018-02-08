// stdafx.h : 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 特定于项目的包含文件
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>

#include <string>
#include <vector>
#include <assert.h>

#include <map>
#include <list>
#include <memory>
#include <map>
#include <queue>
#include <functional>
#include <mutex>
#include <atomic>

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

enum FpError
{
    FpErrorEOF = -0x7fffffff,
    FpErrorGeneric,
    FpErrorInner,
    FpErrorNullptr,
    FpErrorInvalidParams,
    FpErrorTimeOut,

    FpErrorBadState,
    FpErrorBadData,

    FpErrorNotNow,
    FpErrorNotInitialized,

    FpErrorNoData,

    FpErrorOK = 0,
    FpErrorPending,
};

typedef std::string a8string;
typedef std::string u8string;

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