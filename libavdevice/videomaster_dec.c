#include "videomaster_dec.h"

#include "libavcodec/packet_internal.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavformat/demux.h"
#include "libavformat/internal.h"
#include "libavutil/avstring.h"
#include "libavutil/dynarray.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "videomaster_common.h"

#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_Dv.h>

#define OFFSET(x) offsetof(struct VideoMasterData, x)
#define DEC       AV_OPT_FLAG_DECODING_PARAM

/** Static function declaration */
/**
 * @brief Checks the integrity of the audio properties in the
 * VideoMaster context.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
int check_audio_properties(VideoMasterContext *videomaster_context);

/**
 * @brief Checks the integrity of the board index argument in the
 * VideoMaster context.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int check_board_index(VideoMasterContext *videomaster_context);

/**
 * @brief Checks the integrity of the channel
 * index argument in the VideoMaster context. Calling this function may
 * override audio_nb_channels, audio_sample_rate, and audio_sample_size.
 * Call this function after verifying the integrity of the audio properties
 * using the check_audio_properties function.
 * @param videomaster_context VideoMasterContext
 * pointer to the VideoMaster context
 * @return int  0 on success, or negative AVERROR
 * code on failure
 */
static int check_channel_index(VideoMasterContext *videomaster_context);

/**
 * @brief Checks the integrity of all arguments passed in the FFmpeg
 * command-line in the VideoMaster context.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int check_header_arguments(VideoMasterContext *videomaster_context);

/**
 * @brief Checks the integrity of the timestamp source argument in the
 * VideoMaster context.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int check_timestamp_source(VideoMasterContext *videomaster_context);

/**
 * @brief Common error handling for stream operations
 *
 * This function handles errors that occur during stream operations by logging
 * the error message, closing the stream handle, and closing the board handle.
 *
 * @param ctx Pointer to the VideoMaster context
 * @param message Error message to log
 * @param error_code Error code to return
 * @return int 0 on success, or negative AVERROR code on failure
 */
static int handle_stream_error(VideoMasterContext *ctx, const char *message,
                               int error_code);

/**
 * @brief Parses command line arguments for the VideoMaster DELTACAST(c) device.
 *
 * This function extracts the board and channel index from the command line and
 * store them in the context. If a dummy input stream is used, board_index and
 * stream index are taken from the command-line options otherwise, they are
 * deduce from the input name.
 * @param avctx AVFormatContext pointer to the FFmpeg context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int parse_command_line_arguments(AVFormatContext *avctx);

/**
 * @brief  Sets up the FFmpeg audio stream based on the VideoMaster context
 *
 * This function configures the audio stream properties such as sample rate,
 * channel layout, and codec ID. It also sets up the AVStream parameters for
 * audio data.
 *
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int 0 on success, or negative AVERROR code on failure
 */
static int setup_audio_stream(VideoMasterContext *videomaster_context);

/**
 * @brief Sets up the streams for the VideoMaster context.
 *
 * This function initializes the video and audio FFmpeg streams based on the
 * properties defined in the VideoMaster context. It configures the AVStream
 * parameters, codec IDs, and other stream-related settings.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int 0 on success, or negative AVERROR code on failure.
 */
static int setup_streams(VideoMasterContext *videomaster_context);

/**
 * @brief   Sets up the FFmpeg video stream based
 * on the VideoMaster context
 *
 * This function configures the video stream
 * properties such as width, height, frame rate,
 * pixel format, and codec ID. It also sets up
 * the AVStream parameters for video data. The
 * function is called during the initialization
 * phase of the VideoMaster device to ensure that
 * the video stream is properly configured before
 * starting the stream.
 * @param videomaster_context VideoMasterContext
 * pointer to the VideoMaster
 * @return int 0 on success, or negative AVERROR
 * code on failure
 */
static int setup_video_stream(VideoMasterContext *videomaster_context);

/**** Static functions definitions */
int check_audio_properties(VideoMasterContext *videomaster_context)
{
    enum AVVideoMasterChannelType channel_type =
        ff_videomaster_get_channel_type_from_index(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->channel_index);
    if (channel_type == AV_VIDEOMASTER_CHANNEL_HDMI &&
        (videomaster_context->audio_nb_channels != -1 ||
         videomaster_context->audio_sample_rate != -1 ||
         videomaster_context->audio_sample_size != -1))
    {
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Audio properties are not applicable for HDMI channels. These "
               "value will be overridden with auto-detection.\n");
    }
    else
    {
        if (videomaster_context->audio_nb_channels == -1 ||
            videomaster_context->audio_sample_rate ==
                AV_VIDEOMASTER_SAMPLE_RATE_UNKNOWN ||
            videomaster_context->audio_sample_size ==
                AV_VIDEOMASTER_SAMPLE_SIZE_UNKNOWN)
        {
            av_log(videomaster_context->avctx, AV_LOG_WARNING,
                   "Invalid audio properties: "
                   "audio_nb_channels=%d, audio_sample_rate=%s, "
                   "audio_sample_size=%s. Audio will be ignored if audio "
                   "stream is present.\n",
                   videomaster_context->audio_nb_channels,
                   ff_videomaster_sample_rate_to_string(
                       videomaster_context->audio_sample_rate),
                   ff_videomaster_sample_size_to_string(
                       videomaster_context->audio_sample_size));
        }
    }
    return 0;
}
int check_board_index(VideoMasterContext *videomaster_context)
{

    if (videomaster_context->number_of_boards == 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "No DELTACAST boards detected\n");
        return AVERROR(EIO);
    }

    if (videomaster_context->board_index >=
        videomaster_context->number_of_boards)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Invalid board index: %d\n", videomaster_context->board_index);
        return AVERROR(EINVAL);
    }

    av_log(videomaster_context->avctx, AV_LOG_TRACE, "Board index is valid.\n");

    if (ff_videomaster_open_board_handle(videomaster_context) != 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to open board handle.\n");
        return AVERROR(EIO);
    }

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Board handle opened successfully\n");

    return 0;
}

int check_channel_index(VideoMasterContext *videomaster_context)
{
    videomaster_context->has_video = false;
    videomaster_context->has_audio = false;

    if (ff_videomaster_get_nb_rx_channels(videomaster_context) == 0)
    {
        if (videomaster_context->channel_index >=
            videomaster_context->nb_rx_channels)
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Invalid channel index: %d\n",
                   videomaster_context->channel_index);
            return AVERROR(EINVAL);
        }
        else if (!ff_videomaster_is_channel_locked(videomaster_context))
        {
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "Channel %d is not locked\n",
                   videomaster_context->channel_index);
            return 0;
        }
        else
        {
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "Channel index is valid\n");
            if (ff_videomaster_get_video_stream_properties(
                    videomaster_context->avctx,
                    videomaster_context->board_handle,
                    videomaster_context->stream_handle,
                    videomaster_context->channel_index,
                    &videomaster_context->channel_type,
                    &videomaster_context->video_info,
                    &videomaster_context->video_width,
                    &videomaster_context->video_height,
                    &videomaster_context->video_frame_rate_num,
                    &videomaster_context->video_frame_rate_den,
                    &videomaster_context->video_interlaced) == 0)
            {
                videomaster_context->has_video = true;
                float frame_rate =
                    (float)videomaster_context->video_frame_rate_num /
                    videomaster_context->video_frame_rate_den;
                if (videomaster_context->channel_type ==
                    AV_VIDEOMASTER_CHANNEL_HDMI)
                {
                    av_log(
                        videomaster_context->avctx, AV_LOG_TRACE,
                        "Stream properties: %dx%d@%.3f %s %s\n",
                        videomaster_context->video_width,
                        videomaster_context->video_height, frame_rate,
                        VHD_DV_CS_ToPrettyString(
                            videomaster_context->video_info.hdmi.color_space),
                        VHD_DV_SAMPLING_ToPrettyString(
                            videomaster_context->video_info.hdmi
                                .cable_bit_sampling));
                    av_log(videomaster_context->avctx, AV_LOG_TRACE,
                           "Pixel clock: %d\n",
                           videomaster_context->video_info.hdmi.pixel_clock);
                    av_log(videomaster_context->avctx, AV_LOG_TRACE,
                           "Interlaced: %s\n",
                           videomaster_context->video_interlaced ? "true"
                                                                 : "false");
                    av_log(
                        videomaster_context->avctx, AV_LOG_TRACE,
                        "Color space: %s\n",
                        VHD_DV_CS_ToPrettyString(
                            videomaster_context->video_info.hdmi.color_space));
                    av_log(videomaster_context->avctx, AV_LOG_TRACE,
                           "Cable bit sampling: %s\n",
                           VHD_DV_SAMPLING_ToPrettyString(
                               videomaster_context->video_info.hdmi
                                   .cable_bit_sampling));
                }
                else
                {
                    av_log(
                        videomaster_context->avctx, AV_LOG_TRACE,
                        "Stream properties: %dx%d@%.3f %s %s\n",
                        videomaster_context->video_width,
                        videomaster_context->video_height, frame_rate,
                        VHD_VIDEOSTANDARD_ToPrettyString(
                            videomaster_context->video_info.sdi.video_standard),
                        VHD_CLOCKDIVISOR_ToPrettyString(
                            videomaster_context->video_info.sdi.clock_divisor));
                    av_log(videomaster_context->avctx, AV_LOG_TRACE,
                           "Interface: %s\n",
                           VHD_INTERFACE_ToPrettyString(
                               videomaster_context->video_info.sdi.interface));
                    av_log(videomaster_context->avctx, AV_LOG_TRACE,
                           "Genlock offset: %d\n",
                           videomaster_context->video_info.sdi.genlock_offset);
                }

                if (ff_videomaster_open_stream_handle(videomaster_context) == 0)
                {
                    av_log(videomaster_context->avctx, AV_LOG_TRACE,
                           "Stream handle opened successfully\n");
                }
                else
                {
                    av_log(videomaster_context->avctx, AV_LOG_ERROR,
                           "Failed to open stream handle.\n");
                    return AVERROR(EIO);
                }
            }
            else
            {
                av_log(videomaster_context->avctx, AV_LOG_ERROR,
                       "Failed to get stream properties\n");
                return AVERROR(EIO);
            }
            if (ff_videomaster_get_audio_stream_properties(
                    videomaster_context->avctx,
                    videomaster_context->board_handle,
                    videomaster_context->stream_handle,
                    videomaster_context->channel_index,
                    &videomaster_context->channel_type,
                    &videomaster_context->audio_info,
                    &videomaster_context->audio_sample_rate,
                    &videomaster_context->audio_nb_channels,
                    &videomaster_context->audio_sample_size,
                    &videomaster_context->audio_codec) == 0)
            {
                if (videomaster_context->channel_type ==
                    AV_VIDEOMASTER_CHANNEL_HDMI)
                {
                    if (videomaster_context->audio_sample_size != 0 &&
                        videomaster_context->audio_nb_channels != 0)
                    {
                        videomaster_context->has_audio = true;
                        av_log(
                            videomaster_context->avctx, AV_LOG_TRACE,
                            "Audio properties: %d channels @%dHz (%d bits)\n",
                            videomaster_context->audio_nb_channels,
                            videomaster_context->audio_sample_rate,
                            videomaster_context->audio_sample_size);
                    }
                    else
                    {
                        av_log(videomaster_context->avctx, AV_LOG_WARNING,
                               "Audio properties: No audio detected\n");
                    }
                }
                else
                {
                    if (videomaster_context->audio_sample_size !=
                            AV_VIDEOMASTER_SAMPLE_SIZE_UNKNOWN &&
                        videomaster_context->audio_sample_rate !=
                            AV_VIDEOMASTER_SAMPLE_RATE_UNKNOWN &&
                        videomaster_context->audio_nb_channels != 0)
                    {
                        videomaster_context->has_audio = true;
                        av_log(
                            videomaster_context->avctx, AV_LOG_TRACE,
                            "Audio properties: %d channels @%dHz (%d bits)\n",
                            videomaster_context->audio_nb_channels,
                            videomaster_context->audio_sample_rate,
                            videomaster_context->audio_sample_size);
                    }
                    else
                    {
                        av_log(videomaster_context->avctx, AV_LOG_WARNING,
                               "Audio properties: No audio detected\n");
                    }
                }
            }
            else
            {
                av_log(videomaster_context->avctx, AV_LOG_WARNING,
                       "Failed to get audio properties\n");
            }
        }
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to get number of RX channels\n");
        return AVERROR(EIO);
    }

    return 0;
}

int check_header_arguments(VideoMasterContext *videomaster_context)
{

    if (check_board_index(videomaster_context) != 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to check board index integrity\n");
        return AVERROR(EIO);
    }

    if (check_audio_properties(videomaster_context) != 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to check audio properties integrity\n");
        ff_videomaster_close_board_handle(videomaster_context);
        return AVERROR(EIO);
    }

    if (check_channel_index(videomaster_context) != 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to check channel index integrity\n");
        ff_videomaster_close_board_handle(videomaster_context);
        return AVERROR(EIO);
    }

    if (check_timestamp_source(videomaster_context) != 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to check timestamp source integrity\n");
        ff_videomaster_close_stream_handle(videomaster_context);
        ff_videomaster_close_board_handle(videomaster_context);
        return AVERROR(EIO);
    }

    return 0;
}

int check_timestamp_source(VideoMasterContext *videomaster_context)
{
    if (videomaster_context->timestamp_source ==
            AV_VIDEOMASTER_TIMESTAMP_HARDWARE &&
        !ff_videomaster_is_hardware_timestamp_is_supported(videomaster_context))
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Hardware time stamping is not supported on the device. Please "
               "change the value of timestamp_source.\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

int handle_stream_error(VideoMasterContext *ctx, const char *message,
                        int error_code)
{
    av_log(ctx->avctx, AV_LOG_ERROR, "%s\n", message);
    ff_videomaster_close_stream_handle(ctx);
    ff_videomaster_close_board_handle(ctx);
    return error_code;
}

int parse_command_line_arguments(AVFormatContext *avctx)
{
    struct VideoMasterData    *videomaster_data = NULL;
    struct VideoMasterContext *videomaster_context = NULL;

    if (ff_videomaster_extract_context(avctx, &videomaster_data,
                                       &videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to extract context\n");
        return AVERROR(EINVAL);
    }
    else
    {
        if (strcmp(avctx->url, "dummy") == 0)
        {
            av_log(avctx, AV_LOG_TRACE,
                   "Dummy input is selected. Deduce board and channel index "
                   "from command line "
                   "parameter\n");
            if (videomaster_data->board_index == -1)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "Board index is not set. Please use the dedicated "
                       "option when using "
                       "\"dummy\" "
                       "input source.\n");
                return AVERROR(EINVAL);
            }

            if (videomaster_data->channel_index == -1)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "Board index is not set. Please use the dedicated "
                       "option when using "
                       "\"dummy\" "
                       "input source.\n");
                return AVERROR(EINVAL);
            }
            videomaster_context->board_index = videomaster_data->board_index;
            videomaster_context->channel_index =
                videomaster_data->channel_index;
        }
        else
        {
            av_log(avctx, AV_LOG_TRACE,
                   "\"%s\" is selected. Parse string to get board and channel "
                   "index.\n",
                   avctx->url);
            if (sscanf(avctx->url, "stream %d on board %d",
                       &videomaster_context->channel_index,
                       &videomaster_context->board_index) != 2)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "Unknown stream selected : \"%s\". Please use \"ffmpeg "
                       "-sources "
                       "videmaster\" and "
                       "use the correct source name.\n",
                       avctx->url);
                return AVERROR(EINVAL);
            }
        }

        if (videomaster_data->timestamp_source ==
                AV_VIDEOMASTER_TIMESTAMP_OSCILLATOR ||
            videomaster_data->timestamp_source ==
                AV_VIDEOMASTER_TIMESTAMP_SYSTEM ||
            videomaster_data->timestamp_source ==
                AV_VIDEOMASTER_TIMESTAMP_HARDWARE)
        {
            videomaster_context->timestamp_source =
                (enum AVVideoMasterTimeStampType)
                    videomaster_data->timestamp_source;
        }
        else
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid timestamp_source value: %" PRId64 "\n",
                   videomaster_data->timestamp_source);
            return AVERROR(EINVAL);
        }

        videomaster_context->audio_nb_channels = videomaster_data->nb_channels;
        videomaster_context->audio_sample_rate = videomaster_data->sample_rate;
        videomaster_context->audio_sample_size = videomaster_data->sample_size;

        switch (videomaster_data->sample_size)
        {
        case AV_VIDEOMASTER_SAMPLE_SIZE_16:
            videomaster_context->audio_codec = AV_CODEC_ID_PCM_S16LE;
            break;
        case AV_VIDEOMASTER_SAMPLE_SIZE_24:
            videomaster_context->audio_codec = AV_CODEC_ID_PCM_S24LE;
            break;
        }
    }

    av_log(avctx, AV_LOG_INFO,
           "Board index: %d, Stream index: %d, Timestamp source: %s\n",
           videomaster_context->board_index, videomaster_context->channel_index,
           ff_videomaster_timestamp_type_to_string(
               videomaster_context->timestamp_source));

    return 0;
}

int setup_audio_stream(VideoMasterContext *videomaster_context)
{
    if (videomaster_context->has_audio)
    {
        AVStream *av_stream = avformat_new_stream(videomaster_context->avctx,
                                                  NULL);
        if (!av_stream)
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Failed to create new stream\n");
            return AVERROR(ENOMEM);
        }
        av_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        av_stream->codecpar->codec_id = videomaster_context->audio_codec;
        av_stream->codecpar->sample_rate =
            videomaster_context->audio_sample_rate;
        av_stream->codecpar->ch_layout.nb_channels =
            videomaster_context->audio_nb_channels;
        avpriv_set_pts_info(av_stream, 64, 1, 1000000); /* 64 bits pts in us */
        videomaster_context->audio_stream = av_stream;
    }

    return 0;
}

int setup_streams(VideoMasterContext *videomaster_context)
{
    int error_code = setup_video_stream(videomaster_context);
    if (error_code != 0)
        return handle_stream_error(videomaster_context,
                                   "Failed to setup video stream", error_code);
    error_code = setup_audio_stream(videomaster_context);
    if (error_code != 0)
        return handle_stream_error(videomaster_context,
                                   "Failed to setup audio stream", error_code);

    return 0;
}

int setup_video_stream(VideoMasterContext *videomaster_context)
{
    if (videomaster_context->has_video)
    {
        AVStream *av_stream = avformat_new_stream(videomaster_context->avctx,
                                                  NULL);
        if (!av_stream)
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Failed to create new stream\n");
            return AVERROR(ENOMEM);
        }
        av_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        av_stream->codecpar->width = videomaster_context->video_width;
        av_stream->codecpar->height = videomaster_context->video_height;
        av_stream->time_base.den = videomaster_context->video_frame_rate_num;
        av_stream->time_base.num = videomaster_context->video_frame_rate_den;
        av_stream->r_frame_rate =
            av_make_q(videomaster_context->video_frame_rate_num,
                      videomaster_context->video_frame_rate_den);
        av_stream->codecpar->bit_rate = videomaster_context->video_bit_rate;
        av_stream->codecpar->codec_id = videomaster_context->video_codec;
        av_stream->codecpar->format = videomaster_context->video_pixel_format;
        if (videomaster_context->video_interlaced)
        {
            av_stream->codecpar->field_order = AV_FIELD_TT;
        }
        else
        {
            av_stream->codecpar->field_order = AV_FIELD_PROGRESSIVE;
        }

        avpriv_set_pts_info(av_stream, 64, 1, 1000000); /* 64 bits pts in us */

        videomaster_context->video_stream = av_stream;
    }

    return 0;
}

/**** Public functions definitions */
int ff_videomaster_list_input_devices(AVFormatContext         *avctx,
                                      struct AVDeviceInfoList *device_list)
{

    struct VideoMasterData    *videomaster_data = NULL;
    struct VideoMasterContext *videomaster_context = NULL;

    if (ff_videomaster_extract_context(avctx, &videomaster_data,
                                       &videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to extract context\n");
        return AVERROR(EINVAL);
    }

    if (!device_list)
    {
        av_log(avctx, AV_LOG_ERROR, "device_list is NULL!\n");
        return AVERROR(EINVAL);
    }

    if (ff_videomaster_get_api_info(videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get API version or number of boards\n");
        return AVERROR(EIO);
    }

    if (videomaster_context->number_of_boards == 0)
    {
        av_log(avctx, AV_LOG_INFO, "No DELTACAST boards detected\n");
        return AVERROR(EIO);
    }

    for (uint32_t i = 0; i < videomaster_context->number_of_boards; i++)
    {

        if (ff_videomaster_create_devices_infos_from_board_index(
                videomaster_context, i, &device_list) < 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to create devices infos for board %d\n", i);
            return AVERROR(EIO);
        }
    }

    return 0;
}

int ff_videomaster_read_close(AVFormatContext *avctx)
{
    int                        return_code = 0;
    struct VideoMasterData    *videomaster_data = NULL;
    struct VideoMasterContext *videomaster_context = NULL;
    if (ff_videomaster_extract_context(avctx, &videomaster_data,
                                       &videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to extract context\n");
        return AVERROR(EINVAL);
    }

    if (ff_videomaster_release_data(videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to release data\n");
        return AVERROR(EIO);
    }

    if (ff_videomaster_stop_stream(videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to stop stream\n");
        return_code = AVERROR(EIO);
    }
    else
    {
        av_log(avctx, AV_LOG_TRACE, "Stream stopped successfully\n");
    }

    if (videomaster_context->stream_handle)
    {
        if (ff_videomaster_close_stream_handle(videomaster_context) != 0)
        {
            av_log(avctx, AV_LOG_ERROR, "Failed to close stream handle: %d\n",
                   return_code);
            return_code = AVERROR(EIO);
        }
        else
        {
            av_log(avctx, AV_LOG_TRACE, "Stream handle closed successfully\n");
            videomaster_context->stream_handle = NULL;
        }
    }

    if (videomaster_context->board_handle)
    {
        if (ff_videomaster_close_board_handle(videomaster_context) != 0)
        {
            av_log(avctx, AV_LOG_ERROR, "Failed to close board handle: %d\n",
                   return_code);
            return_code = AVERROR(EIO);
        }
        else
        {
            av_log(avctx, AV_LOG_TRACE, "Stream handle board successfully\n");
            videomaster_context->board_handle = NULL;
        }
    }

    if (videomaster_context)
    {
        av_freep(&videomaster_data->context);
        videomaster_data->context = NULL;
        videomaster_context = NULL;
    }

    return return_code;
}

int ff_videomaster_read_header(AVFormatContext *avctx)
{
    struct VideoMasterData    *videomaster_data = NULL;
    struct VideoMasterContext *videomaster_context = NULL;

    if (ff_videomaster_extract_context(avctx, &videomaster_data,
                                       &videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to extract context\n");
        return AVERROR(EINVAL);
    }

    if (parse_command_line_arguments(avctx) != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to parse command line arguments\n");
        return AVERROR(EINVAL);
    }

    if (ff_videomaster_get_api_info(videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get API version or number of boards\n");
        return AVERROR(EIO);
    }

    if (check_header_arguments(videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to check header arguments integrity\n");
        return AVERROR(EIO);
    }

    if ((videomaster_context->has_video || videomaster_context->has_audio) &&
        (ff_videomaster_start_stream(videomaster_context) != 0))
    {
        return handle_stream_error(videomaster_context,
                                   "Failed to start stream\n", AVERROR(EIO));
    }

    if (setup_streams(videomaster_context) != 0)
    {
        return handle_stream_error(videomaster_context,
                                   "Failed to setup Audio and Video streams\n",
                                   AVERROR(EIO));
    }

    videomaster_context->return_video_next = true;

    return 0;
}

int ff_videomaster_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    struct VideoMasterData    *videomaster_data = NULL;
    struct VideoMasterContext *videomaster_context = NULL;

    if (ff_videomaster_extract_context(avctx, &videomaster_data,
                                       &videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to extract context\n");
        return AVERROR(EINVAL);
    }

    if (videomaster_context->return_video_next)
    {
        videomaster_context->return_video_next = false;
        if (ff_videomaster_get_data(videomaster_context) != 0)
        {
            av_log(avctx, AV_LOG_ERROR, "Failed to get data buffers\n");
            return AVERROR(EIO);
        }
        if (videomaster_context->has_video)
        {
            if (av_new_packet(pkt, videomaster_context->video_buffer_size) < 0)
            {

                av_log(avctx, AV_LOG_ERROR,
                       "Failed to allocate AVPacket for Video\n");
                return AVERROR(ENOMEM);
            }
            else
            {
                memcpy(pkt->data, videomaster_context->video_buffer,
                       videomaster_context->video_buffer_size);
                pkt->stream_index = videomaster_context->video_stream->index;
                ff_videomaster_get_timestamp(videomaster_context,
                                             &videomaster_context->pts);
                pkt->pts = videomaster_context->pts;
                pkt->dts = pkt->pts;
                pkt->duration = 1;
            }

            if (ff_videomaster_get_slots_counter(videomaster_context) != 0)
            {
                av_log(avctx, AV_LOG_ERROR, "Failed to get slots counter\n");
                return AVERROR(EIO);
            }
            else
            {
                av_log(avctx, AV_LOG_TRACE, "%u frames received (%u dropped)\n",
                       videomaster_context->frames_received,
                       videomaster_context->frames_dropped);
            }
        }
    }
    else
    {
        videomaster_context->return_video_next = true;
        if (videomaster_context->has_audio)
        {
            if (av_new_packet(pkt, videomaster_context->audio_buffer_size) < 0)
            {

                av_log(avctx, AV_LOG_ERROR,
                       "Failed to allocate AVPacket for Audio\n");
                return AVERROR(ENOMEM);
            }
            else
            {
                memcpy(pkt->data, videomaster_context->audio_buffer,
                       videomaster_context->audio_buffer_size);
                pkt->stream_index = videomaster_context->audio_stream->index;
                // Assign audio PTS as video PTS + 1 to ensure monotonic packet
                // timestamps across all streams, as required by some FFmpeg
                // muxers and filters.
                pkt->pts = videomaster_context->pts + 1;
                pkt->dts = pkt->pts;
                pkt->duration = 1;
            }
            videomaster_context->audio_frames_received +=
                videomaster_context->audio_buffer_size;

            av_log(avctx, AV_LOG_TRACE, "%u audio frames received\n",
                   videomaster_context->audio_frames_received);
        }
        if (ff_videomaster_release_data(videomaster_context) != 0)
        {
            av_log(avctx, AV_LOG_ERROR, "Failed to release data\n");
            return AVERROR(EIO);
        }
    }

    return 0;
}

static const AVOption options[] = {
    { "board_index",
      "Index of the board to use. Only required when the ffmpeg input is set "
      "to dummy (-i dummy). If the input is a source name (from `ffmpeg "
      "-sources videomaster`), the board index is automatically deduced from "
      "the source name.",
      OFFSET(board_index),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "channel_index",
      "Index of the stream to use. Only required when the ffmpeg input is set "
      "to dummy (-i dummy). If the input is a source name (from `ffmpeg "
      "-sources videomaster`), the stream index is automatically deduced from "
      "the source name.",
      OFFSET(channel_index),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "timestamp_source",
      "Selects the source for video frame timestamps. Options are: 'hw' for "
      "hardware-based timestamps (highest precision, if supported), 'osc' for "
      "the device's internal oscillator, or 'system' for the system clock. Use "
      "'hw' for best synchronization accuracy, 'osc' for stable internal "
      "timing, or 'system' for general-purpose timing. Default is 'osc'.",
      OFFSET(timestamp_source),
      AV_OPT_TYPE_INT64,
      { .i64 = AV_VIDEOMASTER_TIMESTAMP_OSCILLATOR },
      AV_VIDEOMASTER_TIMESTAMP_OSCILLATOR,
      AV_VIDEOMASTER_TIMESTAMP_NB - 1,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "timestamp_source" },
    { "osc",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_TIMESTAMP_OSCILLATOR },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "timestamp_source" },
    { "system",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_TIMESTAMP_SYSTEM },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "timestamp_source" },
    { "hw",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_TIMESTAMP_HARDWARE },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "timestamp_source" },
    { "nb_channels",
      "Number of audio channels to use. This option is only used when the "
      "input source is an SDI stream. "
      "If the input source is an HDMI stream, the number of channels is "
      "automatically deduced from the stream properties.",
      OFFSET(nb_channels),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    {
        "sample_rate",
        "Audio sample rate to use. This option is only used when the input "
        "source is an SDI stream. "
        "If the input source is an HDMI stream, the sample rate is "
        "automatically deduced from the stream properties.",
        OFFSET(sample_rate),
        AV_OPT_TYPE_INT64,
        { .i64 = AV_VIDEOMASTER_SAMPLE_RATE_UNKNOWN },
        AV_VIDEOMASTER_SAMPLE_RATE_UNKNOWN,
        AV_VIDEOMASTER_SAMPLE_RATE_48000,
        AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_AUDIO_PARAM,
        .unit = "sample_rate_value",
    },
    { "48000",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_RATE_48000 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "sample_rate_value" },
    { "44100",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_RATE_44100 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "sample_rate_value" },
    { "32000",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_RATE_32000 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "sample_rate_value" },
    {
        "sample_size",
        "Audio sample size to use. This option is only used when the input "
        "source is an SDI stream. "
        "If the input source is an HDMI stream, the sample size is "
        "automatically deduced from the stream properties."
        "Options are: 16 or 24 bits.",
        OFFSET(sample_size),
        AV_OPT_TYPE_INT64,
        { .i64 = AV_VIDEOMASTER_SAMPLE_SIZE_UNKNOWN },
        AV_VIDEOMASTER_SAMPLE_SIZE_UNKNOWN,
        AV_VIDEOMASTER_SAMPLE_SIZE_24,
        AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_AUDIO_PARAM,
        .unit = "sample_size_value",
    },
    { "16",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_SIZE_16 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "sample_size_value" },
    { "24",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_SIZE_24 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | DEC | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "sample_size_value" },
    { NULL },
};

static const AVClass videomaster_demuxer_class = {
    .class_name = "DELTACAST Videomaster indev",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
    .category = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

const FFInputFormat ff_videomaster_demuxer = {
    .p.name = "videomaster",
    .p.long_name = NULL_IF_CONFIG_SMALL("DELTACAST Videomaster input"),
    .p.flags = AVFMT_NOFILE,
    .p.priv_class = &videomaster_demuxer_class,
    .priv_data_size = sizeof(struct VideoMasterData),
    .get_device_list = ff_videomaster_list_input_devices,
    .read_header = ff_videomaster_read_header,
    .read_packet = ff_videomaster_read_packet,
    .read_close = ff_videomaster_read_close,
};