#include "videomaster_dec.h"
#include "libavformat/demux.h"
#include "libavutil/opt.h"

int ff_videomaster_read_header(AVFormatContext *avctx)
{
    av_log(avctx, AV_LOG_ERROR, "ff_videomaster_read_header\n");
    return 0;
}

int ff_videomaster_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    av_log(avctx, AV_LOG_ERROR, "ff_videomaster_read_packet\n");
    return 0;
}

int ff_videomaster_read_close(AVFormatContext *avctx)
{
    av_log(avctx, AV_LOG_ERROR, "ff_videomaster_read_close\n");
    return 0;
}

int ff_videomaster_list_input_devices(AVFormatContext *avctx, struct AVDeviceInfoList *device_list)
{
    av_log(avctx, AV_LOG_ERROR, "ff_videomaster_list_input_devices\n");
    return 0;
}

static const AVOption options [] = {};

static const AVClass videomaster_demuxer_class = {
    .class_name = "DELTACAST Videomaster indev",
    .item_name  = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
    .category = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

const FFInputFormat ff_videomaster_demuxer = {
    .p.name = "videomaster",
    .p.long_name = NULL_IF_CONFIG_SMALL("DELTACAST Videomaster input"),
    .p.flags = AVFMT_NOFILE,
    .p.priv_class = &videomaster_demuxer_class,
    .priv_data_size = 0, //sizeof(struct VideoMasterData), 
    .get_device_list = ff_videomaster_list_input_devices, 
    .read_header = ff_videomaster_read_header, 
    .read_packet = ff_videomaster_read_packet, 
    .read_close = ff_videomaster_read_close, 
};