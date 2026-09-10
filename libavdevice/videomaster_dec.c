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
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "videomaster_common.h"
#include "videomaster_hdmi.h"
#include "videomaster_ip.h"
#include "videomaster_sdi.h"

#if defined(__APPLE__)
#include <VideoMasterHD/VideoMasterHD_Core.h>
#include <VideoMasterHD/VideoMasterHD_Dv.h>
#else
#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_Dv.h>
#endif

#define OFFSET(x) offsetof(struct VideoMasterData, x)

/** Static function declaration */
/**
 * @brief Checks the integrity of the audio properties in the
 * VideoMaster context.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int check_audio_properties(VideoMasterContext *videomaster_context);

/**
 * @brief Checks the integrity of the board index argument in the
 * VideoMaster context.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int check_board_index(VideoMasterContext *videomaster_context);

/**
 * @brief Checks the integrity of the channel index argument in the
 * VideoMaster context.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int check_channel_index(VideoMasterContext *videomaster_context);

/**
 * @brief Validates channel settings in the VideoMaster context.
 *
 *        This function may update audio_nb_channels, audio_sample_rate, and
 *        audio_sample_size. Call it only after check_audio_properties() and
 *        check_channel_index() have succeeded.
 *        It verifies channel lock state, retrieves stream properties, and may
 *        open a stream handle.
 *
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int check_channel_integrity(VideoMasterContext *videomaster_context);

/**
/**
 * @brief Checks the integrity of all arguments passed in the FFmpeg
 * command-line in the VideoMaster context.
 * @param videomaster_data VideoMasterData pointer to parsed command-line
 * options.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int check_header_arguments(VideoMasterData    *videomaster_data,
                                  VideoMasterContext *videomaster_context);

/**
 * @brief Checks the integrity of the timestamp source argument in the
 * VideoMaster context.
 * @param videomaster_context VideoMasterContext pointer to the VideoMaster
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int check_timestamp_source(VideoMasterContext *videomaster_context);

/**
 * @brief   Extracts the VideoMaster context from the AVFormatContext or logs an
 * error if it fails.
 *
 * @param avctx AVFormatContext pointer to the FFmpeg context
 * @param videomaster_data VideoMasterData pointer to store extracted data
 * @param videomaster_context VideoMasterContext pointer to store extracted
 * context
 * @return int  0 on success, or negative AVERROR code on failure
 */
static int extract_context_or_log(AVFormatContext     *avctx,
                                  VideoMasterData    **videomaster_data,
                                  VideoMasterContext **videomaster_context);

/**
 * @brief Common error handling for board-only operations
 *
 * This function handles errors that occur before the stream is started by
 * logging the error message and closing the board handle.
 *
 * @param videomaster_context Pointer to the VideoMaster context
 * @param message Error message to log
 * @param error_code Error code to return
 * @return int 0 on success, or negative AVERROR code on failure
 */
static int handle_board_error(VideoMasterContext *videomaster_context,
                              const char *message, int error_code);

/**
 * @brief Common error handling for stream operations
 *
 * This function handles errors that occur during stream operations by logging
 * the error message, closing the stream handle, and closing the board handle.
 *
 * @param videomaster_context Pointer to the VideoMaster context
 * @param message Error message to log
 * @param error_code Error code to return
 * @return int 0 on success, or negative AVERROR code on failure
 */
static int handle_stream_error(VideoMasterContext *videomaster_context,
                               const char *message, int error_code);

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

/**
 * @brief Reorders a field-sequential frame into line-interleaved layout.
 *
 * Input layout: all top-field lines first, then all bottom-field lines.
 * Output layout: alternating lines (top0, bottom0, top1, bottom1, ...).
 */
static void
interleave_sequential_fields(const VideoMasterContext *videomaster_context,
                             uint8_t *dst, const uint8_t *src)
{
    uint32_t       height = videomaster_context->video_height;
    uint32_t       stride = videomaster_context->video_buffer_size / height;
    const uint8_t *top = src;
    const uint8_t *bottom = src + (height / 2) * stride;

    for (uint32_t i = 0; i < height / 2; i++)
    {
        memcpy(dst + (2 * i) * stride, top + i * stride, stride);
        memcpy(dst + (2 * i + 1) * stride, bottom + i * stride, stride);
    }
}

/**** Static functions definitions */
static int check_audio_properties(VideoMasterContext *videomaster_context)
{
    enum AVVideoMasterChannelType channel_type =
        ff_videomaster_get_channel_type_from_index(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->channel_index);

    if (channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
        return ff_videomaster_check_audio_properties_hdmi(videomaster_context);
    else if (channel_type == AV_VIDEOMASTER_CHANNEL_IP_2110)
        return ff_videomaster_check_audio_properties_ip(videomaster_context);
    else
        return ff_videomaster_check_audio_properties_sdi(videomaster_context);
}

static int check_board_index(VideoMasterContext *videomaster_context)
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
               "Invalid board index: %u\n", videomaster_context->board_index);
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

static int check_channel_index(VideoMasterContext *videomaster_context)
{
    if (ff_videomaster_get_nb_rx_channels(videomaster_context) == 0)
    {
        if (videomaster_context->channel_index >=
            videomaster_context->nb_rx_channels)
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Invalid channel index: %u\n",
                   videomaster_context->channel_index);
            return AVERROR(EINVAL);
        }

        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Channel index is valid.\n");
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to get number of RX channels\n");
        return AVERROR(EIO);
    }
    return 0;
}

static int check_channel_integrity(VideoMasterContext *videomaster_context)
{
    videomaster_context->has_video = false;
    videomaster_context->has_audio = false;

    /* Early exit: channel not locked and not IP */
    if (videomaster_context->channel_type != AV_VIDEOMASTER_CHANNEL_IP_2110 &&
        !ff_videomaster_is_channel_locked(videomaster_context) &&
        !videomaster_context->dual_stream)
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Channel %u is not locked\n",
               videomaster_context->channel_index);
        return 0;
    }

    /* Tech-specific validation and property retrieval */
    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_IP_2110)
        return ff_videomaster_check_channel_integrity_ip(videomaster_context);
    else if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
        return ff_videomaster_check_channel_integrity_hdmi(videomaster_context);
    else
        return ff_videomaster_check_channel_integrity_sdi(videomaster_context);
}

static int check_header_arguments(VideoMasterData    *videomaster_data,
                                  VideoMasterContext *videomaster_context)
{
    int status = 0;
    if ((status = check_board_index(videomaster_context)) != 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to check board index integrity\n");
        return status;
    }

    if ((status = check_audio_properties(videomaster_context)) != 0)
    {
        return handle_board_error(videomaster_context,
                                  "Failed to check audio properties integrity",
                                  status);
    }

    if ((status = check_channel_index(videomaster_context)) != 0)
    {
        return handle_board_error(videomaster_context,
                                  "Failed to check channel index range",
                                  status);
    }

    videomaster_context->channel_type =
        ff_videomaster_get_channel_type_from_index(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->channel_index);

    /* Validate tech-specific arguments after channel_type is known */
    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {
        if ((status = ff_videomaster_validate_arguments_hdmi(
                 videomaster_data, videomaster_context)) != 0)
        {
            return handle_board_error(
                videomaster_context,
                "Invalid or missing arguments for HDMI channel", status);
        }
    }
    else if (videomaster_context->channel_type ==
             AV_VIDEOMASTER_CHANNEL_IP_2110)
    {
        if ((status = ff_videomaster_validate_arguments_ip(
                 videomaster_data, videomaster_context)) != 0)
        {
            return handle_board_error(
                videomaster_context,
                "Invalid or missing arguments for IP 2110 channel", status);
        }
    }
    else
    {
        if ((status = ff_videomaster_validate_arguments_sdi(
                 videomaster_data, videomaster_context)) != 0)
        {
            return handle_board_error(
                videomaster_context,
                "Invalid or missing arguments for SDI channel", status);
        }
    }

    if ((status = check_channel_integrity(videomaster_context)) != 0)
    {
        return handle_board_error(videomaster_context,
                                  "Failed to check channel index integrity",
                                  status);
    }

    if ((status = check_timestamp_source(videomaster_context)) != 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to check timestamp source integrity\n");
        ff_videomaster_close_stream_handle(videomaster_context);
        ff_videomaster_close_board_handle(videomaster_context);
        return status;
    }

    return 0;
}

static int check_timestamp_source(VideoMasterContext *videomaster_context)
{
    VHD_TIMECODE  time_code;
    BOOL32        ltc_source_is_locked;
    float         ltc_source_frame_rate;
    VHD_ERRORCODE error_code;

    if (videomaster_context->timestamp_source ==
            AV_VIDEOMASTER_TIMESTAMP_HARDWARE &&
        !ff_videomaster_is_hardware_timestamp_supported(videomaster_context))
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Hardware time stamping is not supported on the device. Please "
               "change the value of timestamp_source.\n");
        return AVERROR(EINVAL);
    }
    else if (videomaster_context->timestamp_source ==
             AV_VIDEOMASTER_TIMESTAMP_LTC_COMPANION_CARD)
    {
        if (!ff_videomaster_is_ltc_companion_card_supported(
                videomaster_context))
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "LTC companion card feature is not supported on the device. "
                   "LTC companion card timestamp sources is "
                   "not available. Please change the value of "
                   "timestamp_source.\n");
            return AVERROR(EINVAL);
        }
        else
        {
            if (!ff_videomaster_is_ltc_companion_card_present(
                    videomaster_context))
            {
                av_log(videomaster_context->avctx, AV_LOG_ERROR,
                       "LTC companion card is not detected. Please check your "
                       "hardware configuration or change the value "
                       "of timestamp_source.\n");
                return AVERROR(EINVAL);
            }
        }
    }
    else if (videomaster_context->timestamp_source ==
             AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD)
    {
        if (!ff_videomaster_is_ltc_on_board_timestamp_supported(
                videomaster_context))
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "LTC on-board feature is not supported on the device. LTC "
                   "on-board timestamp source is not available. Please change "
                   "the value of timestamp_source.\n");
            return AVERROR(EINVAL);
        }
    }

    if (videomaster_context->timestamp_source ==
            AV_VIDEOMASTER_TIMESTAMP_LTC_COMPANION_CARD ||
        videomaster_context->timestamp_source ==
            AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD)
    {
        error_code = VHD_GetTimecode(
            videomaster_context->board_handle,
            (videomaster_context->timestamp_source ==
             AV_VIDEOMASTER_TIMESTAMP_LTC_COMPANION_CARD)
                ? VHD_TC_SRC_LTC_COMPANION_CARD
                : VHD_TC_SRC_LTC_ONBOARD,
            &ltc_source_is_locked, &ltc_source_frame_rate, &time_code);
        if (error_code == VHDERR_NOERROR)
        {
            av_log(videomaster_context->avctx, AV_LOG_DEBUG,
                   "LTC Time code: %02d:%02d:%02d:%02d\n", time_code.Hour,
                   time_code.Minute, time_code.Second, time_code.Frame);
            if (ltc_source_is_locked)
            {
                av_log(videomaster_context->avctx, AV_LOG_DEBUG,
                       "LTC source is locked at %.3f fps.\n",
                       ltc_source_frame_rate);
                if (videomaster_context->has_video)
                {
                    float video_frame_rate =
                        (float)videomaster_context->video_frame_rate_num /
                        videomaster_context->video_frame_rate_den;
                    if (ltc_source_frame_rate != video_frame_rate)
                    {
                        av_log(videomaster_context->avctx, AV_LOG_WARNING,
                               "LTC frame rate (%.3f fps) does not match "
                               "video frame rate (%.3f fps). Timecode and pts "
                               "deduced from it may be "
                               "incorrect.\n",
                               ltc_source_frame_rate, video_frame_rate);
                    }
                    else
                    {
                        videomaster_context->ltc_frame_rate =
                            ltc_source_frame_rate;
                    }
                }
            }
            else
            {
                av_log(videomaster_context->avctx, AV_LOG_WARNING,
                       "LTC source is not locked. No timecode will be "
                       "available until the LTC source is locked.\n");
            }
        }
        else
        {
            char pLastErrorMessage[VHD_MAX_ERROR_STRING_SIZE] = { 0 };
            VHD_GetLastErrorMessage(pLastErrorMessage,
                                    VHD_MAX_ERROR_STRING_SIZE);
            av_log(videomaster_context->avctx, AV_LOG_DEBUG,
                   "VHDERR = %d - %s\n%s\n", error_code,
                   VHD_ERRORCODE_ToPrettyString(error_code), pLastErrorMessage);
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Cannot get LTC timecode.\n");
            return AVERROR(EIO);
        }
    }
    return 0;
}

static int extract_context_or_log(AVFormatContext     *avctx,
                                  VideoMasterData    **videomaster_data,
                                  VideoMasterContext **videomaster_context)
{
    if (ff_videomaster_extract_context(avctx, videomaster_data,
                                       videomaster_context) != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to extract context\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int handle_board_error(VideoMasterContext *videomaster_context,
                              const char *message, int error_code)
{
    av_log(videomaster_context->avctx, AV_LOG_ERROR, "%s\n", message);
    ff_videomaster_close_board_handle(videomaster_context);
    return error_code;
}

static int handle_stream_error(VideoMasterContext *videomaster_context,
                               const char *message, int error_code)
{
    av_log(videomaster_context->avctx, AV_LOG_ERROR, "%s\n", message);
    ff_videomaster_close_stream_handle(videomaster_context);
    ff_videomaster_close_board_handle(videomaster_context);
    return error_code;
}

static int parse_command_line_arguments(AVFormatContext *avctx)
{
    struct VideoMasterData    *videomaster_data = NULL;
    struct VideoMasterContext *videomaster_context = NULL;

    if (extract_context_or_log(avctx, &videomaster_data,
                               &videomaster_context) != 0)
    {
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
            if (sscanf(avctx->url, "stream %u on board %u",
                       &videomaster_context->channel_index,
                       &videomaster_context->board_index) != 2)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "Unknown stream selected : \"%s\". Please use \"ffmpeg "
                       "-sources "
                       "videomaster\" and "
                       "use the correct source name.\n",
                       avctx->url);
                return AVERROR(EINVAL);
            }
        }

        if (videomaster_data->timestamp_source >= 0 &&
            videomaster_data->timestamp_source < AV_VIDEOMASTER_TIMESTAMP_NB)
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

        videomaster_context->video_buffer_packing =
            videomaster_data->buffer_packing;
        videomaster_context->dual_stream = videomaster_data->dual_stream;

        /* SDP mode and explicit mode are mutually exclusive. */
        {
            bool has_sdp = videomaster_data->ip_video_sdp_file != NULL;
            bool has_explicit = videomaster_data->ip_video_destination != NULL;

            if (has_sdp && has_explicit)
            {
                av_log(
                    avctx, AV_LOG_ERROR,
                    "ip_video_sdp_file and ip_video_destination are mutually "
                    "exclusive.\n");
                return AVERROR(EINVAL);
            }

            if (has_sdp)
            {
                int ret = ff_videomaster_parse_sdp_file(videomaster_data,
                                                        videomaster_context);
                if (ret < 0)
                    return ret;
            }
            else if (has_explicit && videomaster_data->ip_video_width > 0 &&
                     videomaster_data->ip_video_height > 0 &&
                     videomaster_data->ip_video_framerate_num > 0 &&
                     videomaster_data->ip_video_framerate_den > 0 &&
                     videomaster_data->ip_video_interlaced >= 0 &&
                     videomaster_data->ip_video_bit_depth > 0)
            {
                const IPEssenceIn vin = {
                    .dst = videomaster_data->ip_video_destination,
                    .sps_dst = videomaster_data->ip_video_sps_destination,
                    .src = videomaster_data->ip_video_source,
                    .sps_src = videomaster_data->ip_video_sps_source,
                    .udp_port = videomaster_data->ip_video_udp_port,
                    .sps_udp_port = videomaster_data->ip_video_sps_udp_port,
                    .udp_port_src = videomaster_data->ip_video_udp_port_src,
                    .sps_udp_port_src =
                        videomaster_data->ip_video_sps_udp_port_src,
                    .payload_type = videomaster_data->ip_video_payload_type,
                    .sps_payload_type =
                        videomaster_data->ip_video_sps_payload_type,
                };
                const IPEssenceOut vout = {
                    .dst = &videomaster_context->ip_video_destination,
                    .sps_dst = &videomaster_context->ip_video_sps_destination,
                    .src = &videomaster_context->ip_video_source,
                    .sps_src = &videomaster_context->ip_video_sps_source,
                    .udp_port = &videomaster_context->ip_video_udp_port,
                    .sps_udp_port = &videomaster_context->ip_video_sps_udp_port,
                    .udp_port_src = &videomaster_context->ip_video_udp_port_src,
                    .sps_udp_port_src =
                        &videomaster_context->ip_video_sps_udp_port_src,
                    .payload_type = &videomaster_context->ip_video_payload_type,
                    .sps_payload_type =
                        &videomaster_context->ip_video_sps_payload_type,
                };
                int ret = ff_videomaster_parse_ip_essence_network(avctx,
                                                                  "ip_video",
                                                                  &vin, &vout);
                if (ret < 0)
                    return ret;

                if (videomaster_data->ip_video_bit_depth != 8 &&
                    videomaster_data->ip_video_bit_depth != 10)
                {
                    av_log(avctx, AV_LOG_ERROR,
                           "Invalid ip_video_bit_depth value: %" PRId64
                           " (expected 8 or 10)\n",
                           videomaster_data->ip_video_bit_depth);
                    return AVERROR(EINVAL);
                }

                videomaster_context->video_width =
                    (uint32_t)videomaster_data->ip_video_width;
                videomaster_context->video_height =
                    (uint32_t)videomaster_data->ip_video_height;
                videomaster_context->video_frame_rate_num =
                    (uint32_t)videomaster_data->ip_video_framerate_num;
                videomaster_context->video_frame_rate_den =
                    (uint32_t)videomaster_data->ip_video_framerate_den;
                videomaster_context->video_interlaced =
                    !!videomaster_data->ip_video_interlaced;
                videomaster_context->ip_video_depth =
                    videomaster_data->ip_video_bit_depth == 10
                        ? VHD_ST2110_20_DEPTH_10BIT
                        : VHD_ST2110_20_DEPTH_8BIT;
            }
        }
    }

    av_log(avctx, AV_LOG_INFO,
           "Board index: %u, Stream index: %u, Timestamp source: %s, Selected "
           "buffer packing: %s\n",
           videomaster_context->board_index, videomaster_context->channel_index,
           ff_videomaster_timestamp_type_to_string(
               videomaster_context->timestamp_source),
           VHD_BUFFERPACKING_ToPrettyString(
               videomaster_context->video_buffer_packing));

    if (videomaster_context->ip_video_destination != 0 &&
        !videomaster_context->ip_video_sdp_mode)
    {
        av_log(avctx, AV_LOG_INFO,
               "IP 2110 explicit mode: destination=%s, udp_port=%u, "
               "source=%s, udp_port_src=%u, "
               "payload_type=%u, video=%ux%u@%u/%u %s, depth=%s\n",
               videomaster_data->ip_video_destination,
               videomaster_context->ip_video_udp_port,
               videomaster_data->ip_video_source
                   ? videomaster_data->ip_video_source
                   : "any",
               videomaster_context->ip_video_udp_port_src,
               videomaster_context->ip_video_payload_type,
               videomaster_context->video_width,
               videomaster_context->video_height,
               videomaster_context->video_frame_rate_num,
               videomaster_context->video_frame_rate_den,
               videomaster_context->video_interlaced ? "interlaced"
                                                     : "progressive",
               videomaster_context->ip_video_depth == VHD_ST2110_20_DEPTH_10BIT
                   ? "10-bit"
                   : "8-bit");

        if (videomaster_context->ip_video_sps_destination != 0)
        {
            av_log(avctx, AV_LOG_INFO,
                   "IP 2110 SPS explicit mode: destination=%s, udp_port=%u, "
                   "source=%s, udp_port_src=%u, payload_type=%u\n",
                   videomaster_data->ip_video_sps_destination,
                   videomaster_context->ip_video_sps_udp_port,
                   videomaster_data->ip_video_sps_source
                       ? videomaster_data->ip_video_sps_source
                       : "any",
                   videomaster_context->ip_video_sps_udp_port_src,
                   videomaster_context->ip_video_sps_payload_type);
        }
    }
    else if (videomaster_context->ip_video_sdp_mode)
    {
        av_log(avctx, AV_LOG_INFO,
               "IP 2110 SDP mode (main stream): file=%s, "
               "video=%ux%u@%u/%u %s, depth=%s, "
               "main dst=%u.%u.%u.%u:%u pt=%u%s\n",
               videomaster_data->ip_video_sdp_file,
               videomaster_context->video_width,
               videomaster_context->video_height,
               videomaster_context->video_frame_rate_num,
               videomaster_context->video_frame_rate_den,
               videomaster_context->video_interlaced ? "interlaced"
                                                     : "progressive",
               videomaster_context->ip_video_depth == VHD_ST2110_20_DEPTH_10BIT
                   ? "10-bit"
                   : "8-bit",
               (videomaster_context->ip_video_destination >> 24) & 0xFF,
               (videomaster_context->ip_video_destination >> 16) & 0xFF,
               (videomaster_context->ip_video_destination >> 8) & 0xFF,
               videomaster_context->ip_video_destination & 0xFF,
               videomaster_context->ip_video_udp_port,
               videomaster_context->ip_video_payload_type,
               videomaster_context->ip_video_sdp_media_count > 1
                   ? " (SPS present)"
                   : "");

        if (videomaster_context->ip_video_sdp_media_count > 1)
        {
            av_log(avctx, AV_LOG_INFO,
                   "IP 2110 SDP mode (SPS stream): file=%s, "
                   "SPS dst=%u.%u.%u.%u:%u pt=%u\n",
                   videomaster_data->ip_video_sdp_file,
                   (videomaster_context->ip_video_sps_destination >> 24) & 0xFF,
                   (videomaster_context->ip_video_sps_destination >> 16) & 0xFF,
                   (videomaster_context->ip_video_sps_destination >> 8) & 0xFF,
                   videomaster_context->ip_video_sps_destination & 0xFF,
                   videomaster_context->ip_video_sps_udp_port,
                   videomaster_context->ip_video_sps_payload_type);
        }
    }

    /* ---- Audio IP ST2110-30 parsing ---- */
    {
        bool has_audio_explicit = videomaster_data->ip_audio_destination !=
                                  NULL;
        bool has_audio_sdp = videomaster_data->ip_audio_sdp_file != NULL;

        if (has_audio_explicit && has_audio_sdp)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "ip_audio_destination and ip_audio_sdp_file are mutually "
                   "exclusive.\n");
            return AVERROR(EINVAL);
        }

        if (has_audio_sdp)
        {
            int ret = ff_videomaster_parse_audio_sdp_file(videomaster_data,
                                                          videomaster_context);
            if (ret < 0)
                return ret;
        }
        else if (has_audio_explicit)
        {
            const IPEssenceIn ain = {
                .dst = videomaster_data->ip_audio_destination,
                .sps_dst = videomaster_data->ip_audio_sps_destination,
                .src = videomaster_data->ip_audio_source,
                .sps_src = videomaster_data->ip_audio_sps_source,
                .udp_port = videomaster_data->ip_audio_udp_port,
                .sps_udp_port = videomaster_data->ip_audio_sps_udp_port,
                .udp_port_src = videomaster_data->ip_audio_udp_port_src,
                .sps_udp_port_src = videomaster_data->ip_audio_sps_udp_port_src,
                .payload_type = videomaster_data->ip_audio_payload_type,
                .sps_payload_type = videomaster_data->ip_audio_sps_payload_type,
            };
            const IPEssenceOut aout = {
                .dst = &videomaster_context->ip_audio_destination,
                .sps_dst = &videomaster_context->ip_audio_sps_destination,
                .src = &videomaster_context->ip_audio_source,
                .sps_src = &videomaster_context->ip_audio_sps_source,
                .udp_port = &videomaster_context->ip_audio_udp_port,
                .sps_udp_port = &videomaster_context->ip_audio_sps_udp_port,
                .udp_port_src = &videomaster_context->ip_audio_udp_port_src,
                .sps_udp_port_src =
                    &videomaster_context->ip_audio_sps_udp_port_src,
                .payload_type = &videomaster_context->ip_audio_payload_type,
                .sps_payload_type =
                    &videomaster_context->ip_audio_sps_payload_type,
            };
            int ret = ff_videomaster_parse_ip_essence_network(avctx, "ip_audio",
                                                              &ain, &aout);
            if (ret < 0)
                return ret;
        }

        if (has_audio_explicit || has_audio_sdp)
        {
            if (videomaster_data->ip_audio_nb_channels < 1 ||
                videomaster_data->ip_audio_nb_channels > 64)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "ip_audio_nb_channels must be between 1 and 64.\n");
                return AVERROR(EINVAL);
            }
            videomaster_context->ip_audio_format =
                (VHD_ST2110_30_FORMAT)videomaster_data->ip_audio_format;
            videomaster_context->ip_audio_packet_time =
                (VHD_ST2110_30_PACKET_TIME)
                    videomaster_data->ip_audio_packet_time;
            videomaster_context->ip_audio_channel_index =
                videomaster_context->channel_index;
            videomaster_context->audio_nb_channels =
                (uint32_t)videomaster_data->ip_audio_nb_channels;
            videomaster_context->audio_sample_rate =
                VIDEOMASTER_IP_AUDIO_SAMPLE_RATE;
            videomaster_context->audio_sample_size =
                (videomaster_data->ip_audio_format == VHD_ST2110_30_FORMAT_L24)
                    ? AV_VIDEOMASTER_SAMPLE_SIZE_24
                    : AV_VIDEOMASTER_SAMPLE_SIZE_16;
            videomaster_context->audio_codec =
                (videomaster_data->ip_audio_format == VHD_ST2110_30_FORMAT_L24)
                    ? AV_CODEC_ID_PCM_S24LE
                    : AV_CODEC_ID_PCM_S16LE;
        }

        /* Sync mode: requires both video and audio IP streams */
        {
            bool has_video_ip = videomaster_context->ip_video_destination !=
                                    0 ||
                                videomaster_context->ip_video_sdp_mode;
            bool has_audio_ip = videomaster_context->ip_audio_destination !=
                                    0 ||
                                (has_audio_sdp &&
                                 videomaster_context->ip_audio_destination !=
                                     0);
            videomaster_context->ip_sync_mode = has_video_ip && has_audio_ip &&
                                                (videomaster_data->ip_sync !=
                                                 0);
        }
    }

    return 0;
}

static int setup_audio_stream(VideoMasterContext *videomaster_context)
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

static int setup_streams(VideoMasterContext *videomaster_context)
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

static int setup_video_stream(VideoMasterContext *videomaster_context)
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
        av_stream->r_frame_rate =
            av_make_q(videomaster_context->video_frame_rate_num,
                      videomaster_context->video_frame_rate_den);
        av_stream->avg_frame_rate = av_stream->r_frame_rate;
        av_stream->codecpar->bit_rate = videomaster_context->video_bit_rate;
        av_stream->codecpar->codec_id = videomaster_context->video_codec;
        av_stream->codecpar->format = videomaster_context->video_pixel_format;
        av_stream->codecpar->field_order = videomaster_context->video_interlaced
                                               ? AV_FIELD_TT
                                               : AV_FIELD_PROGRESSIVE;

        /* Broadcast content is always limited (studio swing) range. */
        av_stream->codecpar->color_range = AVCOL_RANGE_MPEG;

        /*
         * Derive colour primaries / transfer / matrix from resolution.
         * HD (height >= 720) uses BT.709; SD uses BT.601.
         * This matches ITU-R BT.2081 and common broadcast practice.
         */
        if (videomaster_context->video_height >= 720)
        {
            av_stream->codecpar->color_primaries = AVCOL_PRI_BT709;
            av_stream->codecpar->color_trc = AVCOL_TRC_BT709;
            av_stream->codecpar->color_space = AVCOL_SPC_BT709;
        }
        else
        {
            av_stream->codecpar->color_primaries = AVCOL_PRI_BT470BG;
            av_stream->codecpar->color_trc = AVCOL_TRC_BT709;
            av_stream->codecpar->color_space = AVCOL_SPC_BT470BG;
        }

        avpriv_set_pts_info(av_stream, 64, 1, 1000000); /* 64 bits pts in us */

        videomaster_context->video_stream = av_stream;
    }

    return 0;
}

/**** Public functions definitions */

/* Computes AVPacket duration for a raw PCM audio buffer. */
static int64_t fill_audio_packet_duration(uint32_t buf_size,
                                          uint32_t nb_channels,
                                          uint32_t sample_size,
                                          uint32_t sample_rate)
{
    if (nb_channels == 0 || sample_size == 0 || sample_rate == 0)
        return 1;
    int bpf = ((int)sample_size + 7) / 8 * (int)nb_channels;
    if (bpf <= 0)
        return 1;
    int64_t n = buf_size / bpf;
    int64_t d = av_rescale(n, 1000000, sample_rate);
    return d > 0 ? d : 1;
}

static void unlock_slot_dispatch(VideoMasterContext *ctx, void *slot)
{
    switch (ctx->channel_type)
    {
    case AV_VIDEOMASTER_CHANNEL_IP_2110:
        ff_videomaster_unlock_slot_ip(ctx, slot);
        break;
    case AV_VIDEOMASTER_CHANNEL_SDI:
    case AV_VIDEOMASTER_CHANNEL_ASISDI:
        ff_videomaster_unlock_slot_sdi(ctx, slot);
        break;
    case AV_VIDEOMASTER_CHANNEL_HDMI:
        ff_videomaster_unlock_slot_hdmi(ctx, slot);
        break;
    }
}

int ff_videomaster_list_input_devices(AVFormatContext         *avctx,
                                      struct AVDeviceInfoList *device_list)
{
    struct VideoMasterData    *videomaster_data = NULL;
    struct VideoMasterContext *videomaster_context = NULL;

    if (extract_context_or_log(avctx, &videomaster_data,
                               &videomaster_context) != 0)
    {
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
    if (extract_context_or_log(avctx, &videomaster_data,
                               &videomaster_context) != 0)
    {
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

    av_packet_free(&videomaster_context->pending_packet);

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

    if (extract_context_or_log(avctx, &videomaster_data,
                               &videomaster_context) != 0)
    {
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

    if (check_header_arguments(videomaster_data, videomaster_context) != 0)
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

    return 0;
}

int ff_videomaster_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    struct VideoMasterData    *videomaster_data = NULL;
    struct VideoMasterContext *videomaster_context = NULL;

    if (extract_context_or_log(avctx, &videomaster_data,
                               &videomaster_context) != 0)
    {
        return AVERROR(EINVAL);
    }

    /* Return pre-buffered audio packet from the previous slot */
    if (videomaster_context->pending_packet)
    {
        av_packet_move_ref(pkt, videomaster_context->pending_packet);
        av_packet_free(&videomaster_context->pending_packet);
        return 0;
    }

    uint8_t *video_buf = NULL;
    uint32_t video_size = 0;
    uint8_t *audio_buf = NULL;
    uint32_t audio_size = 0;
    void    *slot = NULL;
    int      lock_ret;

    switch (videomaster_context->channel_type)
    {
    case AV_VIDEOMASTER_CHANNEL_IP_2110:
        lock_ret = ff_videomaster_lock_next_slot_ip(videomaster_context,
                                                    &video_buf, &video_size,
                                                    &audio_buf, &audio_size,
                                                    &slot);
        break;
    case AV_VIDEOMASTER_CHANNEL_SDI:
    case AV_VIDEOMASTER_CHANNEL_ASISDI:
        lock_ret = ff_videomaster_lock_next_slot_sdi(videomaster_context,
                                                     &video_buf, &video_size,
                                                     &audio_buf, &audio_size,
                                                     &slot);
        break;
    case AV_VIDEOMASTER_CHANNEL_HDMI:
        lock_ret = ff_videomaster_lock_next_slot_hdmi(videomaster_context,
                                                      &video_buf, &video_size,
                                                      &audio_buf, &audio_size,
                                                      &slot);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown channel type\n");
        return AVERROR(EINVAL);
    }

    if (lock_ret == AVERROR(EAGAIN))
    {
        av_log(avctx, AV_LOG_WARNING, "Timeout while waiting for slot lock\n");
        return AVERROR(EAGAIN);
    }
    else if (lock_ret != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "Failed to get data buffers\n");
        return AVERROR(EIO);
    }

    /* Fill video packet (primary) */
    if (videomaster_context->has_video && video_buf && video_size > 0)
    {
        if (av_new_packet(pkt, video_size) < 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to allocate AVPacket for Video\n");
            unlock_slot_dispatch(videomaster_context, slot);
            return AVERROR(ENOMEM);
        }
        if (videomaster_context->video_needs_field_reorder &&
            videomaster_context->video_height > 0 &&
            (videomaster_context->video_height % 2) == 0 &&
            (video_size % videomaster_context->video_height) == 0)
            interleave_sequential_fields(videomaster_context, pkt->data,
                                         video_buf);
        else
            memcpy(pkt->data, video_buf, video_size);
        pkt->stream_index = videomaster_context->video_stream->index;
        ff_videomaster_get_timestamp(videomaster_context,
                                     &videomaster_context->pts);
        pkt->pts = videomaster_context->pts;
        pkt->dts = pkt->pts;
        if (videomaster_context->video_frame_rate_num > 0)
        {
            pkt->duration = av_rescale(
                (int64_t)videomaster_context->video_frame_rate_den * 1000000, 1,
                videomaster_context->video_frame_rate_num);
            if (pkt->duration <= 0)
                pkt->duration = 1;
        }
        else
            pkt->duration = 1;

        if (ff_videomaster_get_slots_counter(videomaster_context) != 0)
        {
            av_log(avctx, AV_LOG_ERROR, "Failed to get slots counter\n");
        }
        else
        {
            av_log(avctx, AV_LOG_TRACE, "%u frames received (%u dropped)\n",
                   videomaster_context->frames_received,
                   videomaster_context->frames_dropped);
        }
    }
    else if (videomaster_context->has_audio && audio_buf && audio_size > 0 &&
             videomaster_context->audio_stream)
    {
        /* audio-only path: fill pkt directly (no video stream present) */
        if (av_new_packet(pkt, audio_size) < 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to allocate AVPacket for Audio\n");
            unlock_slot_dispatch(videomaster_context, slot);
            return AVERROR(ENOMEM);
        }
        memcpy(pkt->data, audio_buf, audio_size);
        pkt->stream_index = videomaster_context->audio_stream->index;
        pkt->pts = videomaster_context->pts;
        pkt->dts = pkt->pts;
        pkt->duration = fill_audio_packet_duration(
            audio_size, videomaster_context->audio_nb_channels,
            videomaster_context->audio_sample_size,
            videomaster_context->audio_sample_rate);
        videomaster_context->audio_frames_received += audio_size;
        av_log(avctx, AV_LOG_TRACE, "%u audio frames received\n",
               videomaster_context->audio_frames_received);
    }

    /* Pre-buffer audio packet when both video and audio are present */
    if (videomaster_context->has_video && videomaster_context->has_audio &&
        audio_buf && audio_size > 0 && videomaster_context->audio_stream)
    {
        videomaster_context->pending_packet = av_packet_alloc();
        if (!videomaster_context->pending_packet ||
            av_new_packet(videomaster_context->pending_packet, audio_size) < 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to allocate pending AVPacket for Audio\n");
            av_packet_free(&videomaster_context->pending_packet);
        }
        else
        {
            AVPacket *apkt = videomaster_context->pending_packet;
            memcpy(apkt->data, audio_buf, audio_size);
            apkt->stream_index = videomaster_context->audio_stream->index;
            apkt->pts = videomaster_context->pts;
            apkt->dts = apkt->pts;
            apkt->duration = fill_audio_packet_duration(
                audio_size, videomaster_context->audio_nb_channels,
                videomaster_context->audio_sample_size,
                videomaster_context->audio_sample_rate);
            videomaster_context->audio_frames_received += audio_size;
            av_log(avctx, AV_LOG_TRACE, "%u audio frames received\n",
                   videomaster_context->audio_frames_received);
        }
    }

    unlock_slot_dispatch(videomaster_context, slot);
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
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM |
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
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM |
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
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "timestamp_source" },
    { "osc",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_TIMESTAMP_OSCILLATOR },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "timestamp_source" },
    { "system",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_TIMESTAMP_SYSTEM },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "timestamp_source" },
    { "hw",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_TIMESTAMP_HARDWARE },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "timestamp_source" },
    { "ltc_on_board",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "timestamp_source" },
    { "ltc_companion_card",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_TIMESTAMP_LTC_COMPANION_CARD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM |
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
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
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
        AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
        .unit = "sample_rate_value",
    },
    { "48000",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_RATE_48000 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "sample_rate_value" },
    { "44100",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_RATE_44100 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "sample_rate_value" },
    { "32000",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_RATE_32000 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
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
        AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
        .unit = "sample_size_value",
    },
    { "16",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_SIZE_16 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "sample_size_value" },
    { "24",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_SAMPLE_SIZE_24 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "sample_size_value" },
    {
        "buffer_packing",
        "Specifies the buffer packing format and whether to enable the FPGA's "
        "color space converter on the board. If not set, the default buffer "
        "packing is YUV422 10-bit when the Line Padding  property can be "
        "enabled, or YUV422 8-bit otherwise.",
        OFFSET(buffer_packing),
        AV_OPT_TYPE_INT64,
        { .i64 = AV_NB_VIDEOMASTER_BUFFER_PACKINGS },
        0,
        AV_NB_VIDEOMASTER_BUFFER_PACKINGS,
        AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
        .unit = "buffer_packing_value",
    },
    { "YUV422_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUV422_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "YUVK4224_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUVK4224_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "YUV422_10",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUV422_10 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "YUVK4224_10",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUVK4224_10 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "YUV4444_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUV4444_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "YUVK4444_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUVK4444_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "YUV444_10",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUV444_10 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "YUVK4444_10",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUVK4444_10 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "RGB_32",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_RGB_32 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "RGBA_32",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_RGBA_32 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "RGB_24",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_RGB_24 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YVU420_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU420_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YUV420_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV420_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YVU420_10_MSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU420_10_MSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YVU420_10_LSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU420_10_LSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YUV420_10_MSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV420_10_MSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YUV420_10_LSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV420_10_LSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "RGB_64",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_RGB_64 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "YUV422_16",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUV422_16 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "YUV444_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUV444_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "ICTCP_422_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_ICTCP_422_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "ICTCP_422_10",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_ICTCP_422_10 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YUV422_10_LSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV422_10_LSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YUV422_10_MSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV422_10_MSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YVU422_10_LSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU422_10_LSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YVU422_10_MSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU422_10_MSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YUV422_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV422_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YVU422_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU422_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_YUV422_10_NOPAD_BIGEND",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_YUV422_10_NOPAD_BIGEND },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_PALETTE_RGBA_8",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PALETTE_RGBA_8 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_NV12",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_NV12 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_RGB444_10_LSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_RGB444_10_LSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "RGBA4444_10_LSB_PAD",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_RGBA4444_10_LSB_PAD },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "RGBA4444_16",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_RGBA4444_16 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "PLANAR_P010",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_P010 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      .unit = "buffer_packing_value" },
    { "dual_stream",
      "Force 3G-B dual stream interface (that cannot be auto-detected). A 3G "
      "Level B-DS stream received on the RX0 physical connector is split into "
      "two independent streams: one RX0 stream receives the A link and one RX1 "
      "stream receives the B link.",
      OFFSET(dual_stream),
      AV_OPT_TYPE_BOOL,
      { .i64 = 0 },
      0,
      1,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_destination",
      "IPv4 destination address for main ST2110 video stream in explicit mode.",
      OFFSET(ip_video_destination),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_sps_destination",
      "IPv4 SPS destination address for main ST2110 video stream in explicit "
      "mode.",
      OFFSET(ip_video_sps_destination),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_udp_port",
      "UDP destination port for main ST2110 video stream in explicit mode.",
      OFFSET(ip_video_udp_port),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      65535,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_sps_udp_port",
      "UDP SPS destination port for main ST2110 video stream in explicit mode.",
      OFFSET(ip_video_sps_udp_port),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      65535,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_source",
      "IPv4 source address of the main ST2110 video stream in explicit mode "
      "(unicast RX source filtering). Optional.",
      OFFSET(ip_video_source),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_sps_source",
      "IPv4 source address of the SPS ST2110 video stream in explicit mode "
      "(unicast RX source filtering). Optional.",
      OFFSET(ip_video_sps_source),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_udp_port_src",
      "UDP source port of the main ST2110 video stream in explicit mode. "
      "Optional.",
      OFFSET(ip_video_udp_port_src),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      65535,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_sps_udp_port_src",
      "UDP source port of the SPS ST2110 video stream in explicit mode. "
      "Optional.",
      OFFSET(ip_video_sps_udp_port_src),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      65535,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_payload_type",
      "RTP video payload type for ST2110 stream in explicit mode.",
      OFFSET(ip_video_payload_type),
      AV_OPT_TYPE_INT64,
      { .i64 = 96 },
      0,
      127,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_sps_payload_type",
      "RTP video payload type for SPS ST2110 stream in explicit mode.",
      OFFSET(ip_video_sps_payload_type),
      AV_OPT_TYPE_INT64,
      { .i64 = 96 },
      0,
      127,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_width",
      "Video width for ST2110 stream in explicit mode.",
      OFFSET(ip_video_width),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_height",
      "Video height for ST2110 stream in explicit mode.",
      OFFSET(ip_video_height),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_framerate_num",
      "Video framerate numerator for ST2110 stream in explicit mode.",
      OFFSET(ip_video_framerate_num),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_framerate_den",
      "Video framerate denominator for ST2110 stream in explicit mode.",
      OFFSET(ip_video_framerate_den),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      INT_MAX,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_interlaced",
      "Interlaced flag for ST2110 stream in explicit mode (0 progressive, 1 "
      "interlaced).",
      OFFSET(ip_video_interlaced),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      1,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_bit_depth",
      "Bit depth for ST2110 stream in explicit mode (8 or 10).",
      OFFSET(ip_video_bit_depth),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      10,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    { "ip_video_sdp_file",
      "Path to an SDP file describing the ST2110-20 video stream (main and "
      "optional SPS). Mutually exclusive with ip_video_destination and all "
      "ip_video_* options.",
      OFFSET(ip_video_sdp_file),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM,
      NULL },
    /* ---- Audio ST2110-30 ---- */
    { "ip_audio_destination",
      "IPv4 destination address for the ST2110-30 audio stream.",
      OFFSET(ip_audio_destination),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_sps_destination",
      "IPv4 SPS (redundancy) destination address for the ST2110-30 audio "
      "stream.",
      OFFSET(ip_audio_sps_destination),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_udp_port",
      "UDP destination port for the ST2110-30 audio stream.",
      OFFSET(ip_audio_udp_port),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      65535,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_sps_udp_port",
      "UDP SPS destination port for the ST2110-30 audio stream.",
      OFFSET(ip_audio_sps_udp_port),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      65535,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_source",
      "IPv4 source address for RX filtering of the ST2110-30 audio stream. "
      "Optional.",
      OFFSET(ip_audio_source),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_sps_source",
      "IPv4 SPS source address for RX filtering. Optional.",
      OFFSET(ip_audio_sps_source),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_udp_port_src",
      "UDP source port of the ST2110-30 audio stream. Optional.",
      OFFSET(ip_audio_udp_port_src),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      65535,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_sps_udp_port_src",
      "UDP SPS source port. Optional.",
      OFFSET(ip_audio_sps_udp_port_src),
      AV_OPT_TYPE_INT64,
      { .i64 = -1 },
      -1,
      65535,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_payload_type",
      "RTP payload type for the ST2110-30 audio stream.",
      OFFSET(ip_audio_payload_type),
      AV_OPT_TYPE_INT64,
      { .i64 = 97 },
      0,
      127,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_sps_payload_type",
      "RTP payload type for the ST2110-30 audio SPS (redundancy) stream.",
      OFFSET(ip_audio_sps_payload_type),
      AV_OPT_TYPE_INT64,
      { .i64 = 97 },
      0,
      127,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_nb_channels",
      "Number of audio channels (1-64).",
      OFFSET(ip_audio_nb_channels),
      AV_OPT_TYPE_INT64,
      { .i64 = 2 },
      1,
      64,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_audio_packet_time",
      "RTP packet time for the ST2110-30 audio stream.",
      OFFSET(ip_audio_packet_time),
      AV_OPT_TYPE_INT64,
      { .i64 = VHD_ST2110_30_PACKETTIME_1MS },
      VHD_ST2110_30_PACKETTIME_1MS,
      VHD_ST2110_30_PACKETTIME_125US,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "ip_audio_packet_time_value" },
    { "1ms",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = VHD_ST2110_30_PACKETTIME_1MS },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "ip_audio_packet_time_value" },
    { "125us",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = VHD_ST2110_30_PACKETTIME_125US },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "ip_audio_packet_time_value" },
    { "ip_audio_format",
      "PCM sample format for the ST2110-30 audio stream.",
      OFFSET(ip_audio_format),
      AV_OPT_TYPE_INT64,
      { .i64 = VHD_ST2110_30_FORMAT_L24 },
      VHD_ST2110_30_FORMAT_L16,
      VHD_ST2110_30_FORMAT_L24,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "ip_audio_format_value" },
    { "L16",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = VHD_ST2110_30_FORMAT_L16 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "ip_audio_format_value" },
    { "L24",
      NULL,
      0,
      AV_OPT_TYPE_CONST,
      { .i64 = VHD_ST2110_30_FORMAT_L24 },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      .unit = "ip_audio_format_value" },
    { "ip_audio_sdp_file",
      "Path to a dedicated SDP file describing the ST2110-30 audio stream. "
      "Mutually exclusive with ip_audio_destination.",
      OFFSET(ip_audio_sdp_file),
      AV_OPT_TYPE_STRING,
      { .str = NULL },
      0,
      0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
    { "ip_sync",
      "Enable ST2110 stream synchronization (video+audio). Default on. "
      "Disable for independent essence acquisition.",
      OFFSET(ip_sync),
      AV_OPT_TYPE_BOOL,
      { .i64 = 1 },
      0,
      1,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM |
          AV_OPT_FLAG_AUDIO_PARAM,
      NULL },
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
