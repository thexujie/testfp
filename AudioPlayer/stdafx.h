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

// TODO: 在此处引用程序需要的其他头文件
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include <libavutil/opt.h>
#include "SDL.h"
};
