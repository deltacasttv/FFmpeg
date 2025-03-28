#ifndef AVDEVICE_VIDEOMASTER_DEC_H
#define AVDEVICE_VIDEOMASTER_DEC_H

#include "libavformat/avformat.h"

typedef struct VideoMasterData {
    // AVClass *av_class; //monkey code

    // void *ctx; //monkey code
    int dummy; //to replace

    //needed ?
    // AVPacket *pkt;
} VideoMasterData;

int ff_videomaster_read_header(AVFormatContext *avctx);
int ff_videomaster_read_packet(AVFormatContext *avctx, AVPacket *pkt);
int ff_videomaster_read_close(AVFormatContext *avctx);
int ff_videomaster_list_input_devices(AVFormatContext *avctx, struct AVDeviceInfoList *device_list);
#endif /* AVDEVICE_VIDEOMASTER_DEC_H */
