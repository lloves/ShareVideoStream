#ifndef SHARESTREAM_H
#define SHARESTREAM_H

//extern "C"{

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>

//}
void socketShareData(AVFrame * pFrame, int width, int height);
void *sendSocketData(void *arg);
void saveYUV420Frame(AVFrame * pFrame, int width, int height);

#endif
