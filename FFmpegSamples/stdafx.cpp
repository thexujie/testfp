#include "stdafx.h"

#pragma comment(lib, "sdl2.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swresample.lib")

int decode_audio_main(int argc, char **argv);

int main(int argc, char **argv)
{
    return decode_audio_main(argc, argv);
}
