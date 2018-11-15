/* Need: FFmpeg 4.0+
 * Function: transcoding DTS-HD to LPCM
 * Date: 2018-08-30
 * Author: liwei.sy@star-net.cn
 */

#include <sys/socket.h>
#include <cutils/sockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/un.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include "sharestream.h"


const char program_name[] = "sharestream";
const int program_birth_year = 2018;

#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 // 1s/48kHz/32bit
#define SHARE_FILE_PATH "/system/etc/yuv420sp_frame"
#define PICTURE_WIDTH 640
#define PICTURE_HEIGHT 480
#define PATH "sharestream.localsocket"
int flag = 0;

unsigned char *socketBuf; // = (unsigned char *)malloc((PICTURE_WIDTH * PICTURE_HEIGHT * 3) >> 1);
unsigned char *decodeBuf; // = (unsigned char *)malloc((PICTURE_WIDTH * PICTURE_HEIGHT * 3) >> 1);

pthread_mutex_t mutex;


static void ppm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename)
{
    FILE *f;
    int i;

    f = fopen(filename,"w");
    fprintf(f, "P6\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize*3, f);
    fclose(f);
}

static void decode_video(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame,
				AVFrame *mRGBFrame, /*struct SwsContext *img_convert_ctx,*/ char *filename, int width, int height, int serverID)
{
	/* decode video frame */
	int ret;
	char buf[1024];
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    // start        init socket
	/*
    int serverID = socket_local_server(PATH, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    if(serverID < 0){
        printf("socket_local_server failed :%d\n",serverID);
        //return;
    } else {
		printf("socket_local_server success :%d\n",serverID);
	}*/
    // end          int socket

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
        /*
		sws_scale(img_convert_ctx,
                        (uint8_t const * const *) frame->data,
                        frame->linesize, 0, dec_ctx->height, mRGBFrame->data,
                        mRGBFrame->linesize);
        */

        printf("saving frame %3d\n", dec_ctx->frame_number);
        fflush(stdout);


        /* the picture is allocated by the decoder. no need to free it */

#ifdef DUMP_DEBUG_FILE
        snprintf(buf, sizeof(buf), "%s-%d.ppm", filename, dec_ctx->frame_number);
		if( (dec_ctx->frame_number)%20 == 0 ) {
        	//ppm_save(frame->data[0], frame->linesize[0],
            //     	frame->width, frame->height, buf);
			ppm_save(mRGBFrame->data[0], mRGBFrame->linesize[0],
                  width, height,/*dec_ctx->width, dec_ctx->height,*/ buf);
		}
#endif

        socketShareData(frame, width, height);

        //if(dec_ctx->frame_number == 10)
		//saveYUV420Frame(mRGBFrame, width, height);
        //-------------------------------------------------------//
        // saveYUV420Frame(frame, width, height);

        // start        init socket
        int socketID;
        pthread_t tid;
        if((socketID = accept(serverID,NULL,NULL)) >=0){
			printf("serverId is %d\n", serverID);
            int ret1 = pthread_create(&tid,NULL,sendSocketData,(void *)&socketID);
            if(ret1 != 0){
                printf("error create thread:%s\n",strerror(ret));
                exit(1);
            }
        }
        // end          int socket
    }

}

static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame,
                   FILE *outfile)
{
    int i, ch;
    int ret, data_size;

    /* send the packet with the compressed data to the decoder */
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        printf("Error submitting the packet to the decoder\n");
        exit(1);
    }

    /* read all the output frames (in general there may be any number of them */
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            printf("Error during decoding\n");
            exit(1);
        }
        data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
        if (data_size < 0) {
            /* This should not occur, checking just for paranoia */
            printf("Failed to calculate data size\n");
            exit(1);
        }
		printf("samples %d. channels %d.\n", frame->nb_samples, dec_ctx->channels);
        for (i = 0; i < frame->nb_samples; i++)
            for (ch = 0; ch < dec_ctx->channels; ch++)
                fwrite(frame->data[ch] + data_size*i, 1, data_size, outfile);
    }
}

void saveYUV420Frame(AVFrame * pFrame, int width, int height) {
    FILE *pFile;
    char szFilename[32];
    int  y;
	int i;
    // Open file
    pFile=fopen(SHARE_FILE_PATH, "w+");
    if(pFile==NULL)
        return;

	i = fileno(pFile);
	if(-1 == flock(i,LOCK_EX))
    {
    	printf("failing to lock share frame file !\n ");
    }
	if(0 == flock(i, LOCK_EX))
    {
        printf("lock share frane file success!\n");
    }
    //remove(pFile);

    // 文件清空，重新seek到文件开头
    // ftruncate(pFile, 0);
    //-- lseek64(pFile, 0, SEEK_END);
    // FILE *tmp = freopen(0, "ab", pFile);
    // if (tmp) pFile = tmp;

    // Write pixel data
    for(y=0; y<height ; y++)
        fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width, pFile);
    // --lseek64(pFile, 0, SEEK_END);
    for(y=0; y<height / 2; y++) {
        fwrite(pFrame->data[1]+y*pFrame->linesize[1], 1, width / 2, pFile);
    }
    // --lseek64(pFile, 0, SEEK_END);
    for(y=0; y<height / 2; y++) {
        fwrite(pFrame->data[2]+y*pFrame->linesize[2], 1, width / 2, pFile);
    }
    // Close file
    fclose(pFile);
	flock(i, LOCK_UN);
}


void socketShareData(AVFrame * pFrame, int width, int height) {
    int  y;

    if(flag == 0) {
        pthread_mutex_lock(&mutex);
        for(y=0; y<height ; y++)
            memcpy(decodeBuf + y*width, pFrame->data[0]+y*pFrame->linesize[0], width);
            //fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width, pFile);
        //lseek64(pFile, 0, SEEK_END);
        for(y=0; y<height / 2; y++) {
            // fwrite(pFrame->data[1]+y*pFrame->linesize[1], 1, width / 2, pFile);
            memcpy(decodeBuf + height*width + y*(width/2), pFrame->data[1]+y*pFrame->linesize[1], width/2);
        }
        //lseek64(pFile, 0, SEEK_END);
        for(y=0; y<height / 2; y++) {
            //fwrite(pFrame->data[2]+y*pFrame->linesize[2], 1, width / 2, pFile);
            memcpy(decodeBuf + height*width*5/4 + y*(width/2), pFrame->data[2]+y*pFrame->linesize[2], width/2);
        }
        pthread_mutex_unlock(&mutex);
        flag = 1;

    }


    /*
    if(flag == 0) {
        pthread_mutex_lock(&mutex);
        memcpy(decodeBuf, socketBuf, (PICTURE_WIDTH * PICTURE_HEIGHT * 3) >> 1);
        pthread_mutex_unlock(&mutex);
        flag = 1;
    }*/
}

void *sendSocketData(void *arg) {
    int ret;
    int socketID =*(int *)arg;
    if(socketID < 0){
        printf("socketID is %d\n",socketID);
        return NULL;
    }

    if(flag == 1) {
        pthread_mutex_lock(&mutex);
        // send data;
        memcpy(socketBuf, decodeBuf, (PICTURE_WIDTH * PICTURE_HEIGHT * 3) >> 1);
        pthread_mutex_unlock(&mutex);
        flag = 0;

		FILE *pFile;
        pFile=fopen(SHARE_FILE_PATH, "w+");
        if(pFile==NULL)
            return NULL;
        fwrite(socketBuf, 1, (PICTURE_WIDTH * PICTURE_HEIGHT * 3) >> 1, pFile);
        fclose(pFile);
    }
    // 可能前面的几帧数据会花屏一下
	printf("send data from socket.\n");
    ret = write(socketID, socketBuf, ((PICTURE_WIDTH * PICTURE_HEIGHT * 3) >> 1) * sizeof(unsigned char));
    if(ret < 0){
        printf("write failed\n");
        return NULL;
    }
    close(socketID);
    pthread_exit(NULL);
}

void show_help_default(const char *opt, const char *arg)
{
    //av_log_set_callback(log_callback_help);
    //show_usage();
    //show_help_options(options, "Main options:", 0, 0, 0);
    printf("\n");

    //show_help_children(avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);
    //show_help_children(avcodec_get_class(), AV_OPT_FLAG_DECODING_PARAM);
}

int main(int argc, char** argv) {
	int i = 0;
	int audioStream = -1;
	int videoStream = -1;

	printf("char size is %d, unsigned char size is %d\n", sizeof(char), sizeof(unsigned char));

	if(argc < 3) {
		printf("Usage example: decode2wav input_filename.dts output.wav\n");
		return -1;
	}

    socketBuf = (unsigned char *)malloc((PICTURE_WIDTH * PICTURE_HEIGHT * 3) >> 1);
    decodeBuf = (unsigned char *)malloc((PICTURE_WIDTH * PICTURE_HEIGHT * 3) >> 1);

    av_register_all();
	AVFormatContext *formatContext = NULL;
	AVCodecContext *codecContext = NULL;
	if( avformat_open_input(&formatContext, argv[1], NULL, NULL) != 0 ) {
		printf("open media file fail.\n");
		return -1;
	}
	if( avformat_find_stream_info(formatContext, NULL) != 0 ) {
		printf("Could not find open stream info.\n");
		return -1;
	}
	printf("Stream's count %d \n", formatContext->nb_streams);
	if(formatContext->nb_streams != 1) {
		printf("Stream is more then one.\n");
		//return -1;
	}
	for(i=0; i<(formatContext->nb_streams); i++ ) {
		if(formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStream = i;
			printf("Audio stream is %d\n", audioStream);
			break;
		}
	}

    for(i=0; i<(formatContext->nb_streams); i++ ) {
        if(formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            printf("Video stream is %d\n", videoStream);
            break;
        } else{
			printf("Steame %d is not Video.\n", i);
		}
    }

	if(audioStream == -1)
		printf("Media file has no audio stream.\n");


	if(videoStream == -1)
        printf("Media file has no video stream.\n");

	codecContext = formatContext->streams[i]->codec;

    //AVCodecParameters *pCodecPar = formatContext->streams[videoStream]->codecpar;

	AVCodec *mAVCodec = avcodec_find_decoder(codecContext->codec_id);
    //AVCodec *mAVCodec;
    /*
    switch (codecContext->codec_id){
        case AV_CODEC_ID_H264:
            mAVCodec = avcodec_find_decoder_by_name("h264_mediacodec");//硬解码264
            printf("Codec default H264 decode\n");
            if (mAVCodec == NULL) {
                printf("Couldn't find Codec.\n");
                return -1;
            }
            break;
        case AV_CODEC_ID_MPEG4:
            mAVCodec = avcodec_find_decoder_by_name("mpeg4_mediacodec");//硬解码mpeg4
            printf("Codec default MPEG4 decode\n");
            if (mAVCodec == NULL) {
                printf("Couldn't find Codec.\n");
                return -1;
            }
            break;
        case AV_CODEC_ID_HEVC:
            mAVCodec = avcodec_find_decoder_by_name("hevc_mediacodec");//硬解码265
            printf("Codec default H265 decode\n");
            if (mAVCodec == NULL) {
                printf("Couldn't find Codec.\n");
                return -1;
            }
            break;
        default:
            mAVCodec = avcodec_find_decoder(codecContext->codec_id);//软解
            printf("Codec default software decode\n");
            if (mAVCodec == NULL) {
                printf("Couldn't find Codec.\n");
                return -1;
            }
            break;
    }
    */

    // mAVCodec = avcodec_find_decoder(codecContext->codec_id);//软解
    // 注册编码器的上下文

    /*
    AVCodecContext *pCodecCtx = avcodec_alloc_context3(mAVCodec);
    if (avcodec_parameters_to_context(pCodecCtx, pCodecPar) != 0) {
        printf("Couldn't copy codec context");
        return -1; // Error copying codec context
    }
    */

	if(mAVCodec == NULL) {
		printf("Codec could not find.\n");
		return -1;
	}

	if(avcodec_open2(codecContext, mAVCodec, NULL) <0) {
		printf("Codec open failed.\n");
		return -1;
	}
	printf("Stream bit_rate is %d\n", codecContext->bit_rate);
	printf("Stream sample_rate is %d\n", codecContext->sample_rate);
	printf("Stream channels is %d\n", codecContext->channels);
	printf("Stream codec name %s\n", codecContext->codec->name);
	printf("Stream framerate is {%d, %d}\n", codecContext->framerate.num, codecContext->framerate.den);
    printf("Stream pix format is %d\n", codecContext->pix_fmt);
    //pCodecCtx->framerate.num = 120;

	static AVPacket packet;
	uint8_t *packetData;
    int packetSize;
	FILE *pcmFile;
	int dataLength = AVCODEC_MAX_AUDIO_FRAME_SIZE*100;
	int len = -1;
	int ret;
	//uint8_t *buffer = (uint8_t *)malloc(dataLength);
	AVFrame *frame = av_frame_alloc();

	// conversion src frame data to RGB24 format. .
	// 保存成RGB需要转换一下
    /*
    AVFrame *pFrameRGB = av_frame_alloc();
	int             numBytes;
	uint8_t         *buffer = NULL;
	static struct SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(codecContext->width, codecContext->height,
            codecContext->pix_fmt, 1440, 1080, //codecContext->width, codecContext->height,
    		AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);

	if(pFrameRGB == NULL) {
		printf("frame alloc failed.\n");
		return -1;
	}

	numBytes=avpicture_get_size(AV_PIX_FMT_RGB24, 1440, 1080); //codecContext->width, codecContext->height);
	buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

	// AVFrame is superset of AVPicture.
	avpicture_fill((AVPicture *) pFrameRGB, buffer, AV_PIX_FMT_RGB24,
            1440, 1080);//codecContext->width, codecContext->height);

    */
	// end RGB24 转换

	// conversion src frame data to YUN420SP format. the msm8974 support.
	int             picSize;
    uint8_t         *yuvBuffer = NULL;
    static struct SwsContext *img_YUV420SP_convert_ctx = NULL;
	int srcWidth = codecContext->width;
	int srcHeight = codecContext->height;
	int dspWidth = 1440;
    int dspHeight = 1080;

	AVFrame *frameYUV420SP = av_frame_alloc();
	if(frameYUV420SP == NULL) {
		printf("Frame alloc failed\n");
		return -1;
	}

    /*
	img_YUV420SP_convert_ctx = sws_getContext(
                srcWidth, srcHeight, codecContext->pix_fmt,
                dspWidth, dspHeight, AV_PIX_FMT_YUV420P,
                SWS_BICUBIC, NULL, NULL, NULL);
	picSize = avpicture_get_size(AV_PIX_FMT_YUV420P, dspWidth, dspHeight);
	yuvBuffer = (uint8_t *)av_malloc(picSize*sizeof(uint8_t));
	avpicture_fill((AVPicture *) frameYUV420SP, yuvBuffer, AV_PIX_FMT_YUV420P,
            dspWidth, dspHeight);
    */
	// end YUV420sp转换

	int got_frame;
	pcmFile = fopen(argv[2], "wb");

	int serverID = socket_local_server(PATH, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    if(serverID < 0){
        printf("socket_local_server failed :%d\n",serverID);
        //return;
    } else {
        printf("socket_local_server success :%d\n",serverID);
    }

	while(av_read_frame(formatContext, &packet) >= 0) {
		if(packet.stream_index == audioStream) {
			//packetData = packet.data;
			packetSize = packet.size;
			if(packetSize) {
				if (!frame) {
            		//if (!(frame = av_frame_alloc())) {
                		printf("Could not allocate audio frame\n");
                		exit(1);
            		//}
        		}
				// 暂时屏蔽Audio相关的操作。
				//decode(codecContext, &packet, frame, pcmFile);

				//av_frame_free(&frame);
			}
			/*while(packetSize > 0) {
				got_frame = 0;
				len = avcodec_decode_audio4(codecContext, frame, &got_frame, &packet);
				if(ret < 0) {
					printf("decode audio error.\n");
					break;
				}
				if(got_frame) {
					//int data_size = av_get_bytes_per_sample(frame->format);
					//size_t unpadded_linesize = frame->nb_samples * data_size;
					fwrite(frame->extended_data[0], 1, dataLength*7, pcmFile);
					fflush(pcmFile);
				}
				packetData += len;
				packetSize -= len;
			}*/
		}
		if(packet.stream_index == videoStream) {
			if(packetSize) {
                if (!frame) {
                    //if (!(frame = av_frame_alloc())) {
                        printf("Could not allocate video frame\n");
                        exit(1);
                    //}
                }

                // 暂时屏蔽Video相关的操作。
				// 保存图片为RGB24
                // decode_video(codecContext, &packet, frame, pFrameRGB, img_convert_ctx, "test_00000000");
				// 保存图片为YUV420SP
				// decode_video(codecContext, &packet, frame, frameYUV420SP, img_YUV420SP_convert_ctx, "test_00000000", dspWidth, dspHeight);
                decode_video(codecContext, &packet, frame, frameYUV420SP, "test_00000000", srcWidth, srcHeight, serverID);
                //av_frame_free(&frame);
            }
		}


		//av_free_packet(&packet);
	}
	av_packet_unref(&packet);
	av_frame_free(&frame);
	av_frame_free(&frameYUV420SP);
	//free(buffer);
	fclose(pcmFile);
	if(codecContext != NULL)
		avcodec_close(codecContext);

	//av_free(codecContext);
	avformat_close_input(&formatContext);
	return 0;
}

