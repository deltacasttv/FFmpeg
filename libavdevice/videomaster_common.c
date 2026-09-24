#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"
#include "videomaster_hdmi.h"
#include "videomaster_internal.h"
#include "videomaster_ip.h"
#include "videomaster_sdi.h"
#include <stdio.h>

#if defined(__APPLE__)
#include <VideoMasterHD/VideoMasterHD_Core.h>
#include <VideoMasterHD/VideoMasterHD_Ip_ST2110_Board.h>
#include <VideoMasterHD/VideoMasterHD_PTP.h>
#include <VideoMasterHD/VideoMasterHD_String.h>
#else
#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_Ip_ST2110_Board.h>
#include <VideoMasterHD_PTP.h>
#include <VideoMasterHD_String.h>
#endif

/** Maximum time to wait for the channel to lock once its loopback has been
 * disabled, and the polling period used while waiting. */
#define VIDEOMASTER_LOOPBACK_LOCK_TIMEOUT_MS 5000
#define VIDEOMASTER_LOOPBACK_LOCK_POLL_MS    100

#define VIDEOMASTER_LOCK_SLOT_TIMEOUT_MS 5000

/* Short enough for ff_videomaster_ip_link_watch() to notice a signal loss
 * within a second. */
#define VIDEOMASTER_IP_LOCK_SLOT_TIMEOUT_MS 1000

/** static tables */

/**
 * @brief VideoMaster buffer packing information.
 *
 * This structure holds information about the buffer packing format used
 * for video streams in the VideoMaster context.
 */
typedef struct
{
    enum AVVideoMasterBufferPacking buffer_packing;
    bool codec_or_pixel_format;  ///< true if codec ID or pixel format is used
    union
    {
        enum AVPixelFormat pixel_format;
        enum AVCodecID     codec_id;
    } format;
    int  bits_per_pixel;
    int  bit_rate_num_mul;
    int  bit_rate_den_mul;
    bool line_padding_needed;
} VideoMasterBufferPackingInfo;

static const VideoMasterBufferPackingInfo buffer_packing_info_table[] = {
    { AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_NV12,
      false,
      { .pixel_format = AV_PIX_FMT_NV12 },
      12,
      1,
      1,
      false },
    { AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_P010,
      false,
      { .pixel_format = AV_PIX_FMT_P010LE },
      24,
      1,
      1,
      false },
    { AV_VIDEOMASTER_BUFFER_PACKING_RGB_32,
      false,
      { .pixel_format = AV_PIX_FMT_BGR0 },
      32,
      1,
      1,
      false },
    { AV_VIDEOMASTER_BUFFER_PACKING_RGBA_32,
      false,
      { .pixel_format = AV_PIX_FMT_BGRA },
      32,
      1,
      1,
      false },
    { AV_VIDEOMASTER_BUFFER_PACKING_RGB_24,
      false,
      { .pixel_format = AV_PIX_FMT_BGR24 },
      24,
      1,
      1,
      false },
    { AV_VIDEOMASTER_BUFFER_PACKING_RGB_64,
      false,
      { .pixel_format = AV_PIX_FMT_RGBA64 },
      64,
      1,
      1,
      false },
    { AV_VIDEOMASTER_BUFFER_PACKING_YUV422_8,
      false,
      { .pixel_format = AV_PIX_FMT_UYVY422 },
      16,
      1,
      1,
      false },
    { AV_VIDEOMASTER_BUFFER_PACKING_YUV422_10,
      true,
      { .codec_id = AV_CODEC_ID_V210 },
      64,
      1,
      3,
      true },
};

/** static functions declaration **/

/**
 * @brief Add the device information into the list
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param board_name  Pointer to the variable to store the board name
 * @param serial_number  Pointer to the variable to store the serial number
 * @param device_list  Pointer to the variable to store the device list
 * @return int 0 on success, negative AVERROR code on failure
 */
static int add_device_info_into_list(VideoMasterContext *videomaster_context,
                                     char *board_name, char *serial_number,
                                     struct AVDeviceInfoList **device_list);

/**
 * @brief Create a device info object
 *
 * @param videomaster_context  VideoMasterContext pointer
 * to the VideoMasterContext
 * @param device_name  Name of the device
 * @param device_description  Description of the device
 * @param is_video  Whether the device is a video device
 * @return AVDeviceInfo*  Pointer to the device info
 * object
 */
static AVDeviceInfo *create_device_info(VideoMasterContext *videomaster_context,
                                        char               *device_name,
                                        char               *device_description,
                                        bool                is_video);

/**
 * @brief Select the loopback property to use for the channel
 *
 * Only one loopback mechanism is driven per channel, picked by priority:
 * firmware loopback, then active loopback, then passive loopback (bypass
 * relay), depending on the board capabilities and the channel index.
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @return VHD_CORE_BOARDPROPERTY  The selected loopback property, or
 * NB_VHD_CORE_BOARDPROPERTIES if the channel has no loopback
 */
static VHD_CORE_BOARDPROPERTY
select_loopback_property(VideoMasterContext *videomaster_context);

/**
 * @brief Disable loopback on the channel
 *
 * This function saves the current loopback state of the channel specified in
 * the VideoMasterContext, then disables the loopback if it was enabled. The
 * saved state is restored by restore_loopback_on_channel().
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @return int 0 on success, negative AVERROR code on failure
 */
static int disable_loopback_on_channel(VideoMasterContext *videomaster_context);

/**
 * @brief Restore loopback on the channel
 *
 * This function restores the loopback state saved by
 * disable_loopback_on_channel(). It does nothing if no state was saved, so the
 * loopback is never enabled if it was not enabled before being disabled here.
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @return int 0 on success, negative AVERROR code on failure
 */
static int restore_loopback_on_channel(VideoMasterContext *videomaster_context);

/**
 * @brief    Formats the device description string.
 *
 * This function formats the device description string using the provided
 * board name and serial number and variables stored in videomaster_context.
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param board_name  Name of the board
 * @param serial_number  Serial number of the board
 * @return char*  Formatted device description string
 */
static char *format_device_description(VideoMasterContext *videomaster_context,
                                       const char         *board_name,
                                       const char         *serial_number);

/**
 * @brief Formats a fallback device description string.
 *
 * This function is used when stream auto-detection cannot provide detailed
 * video/audio properties. It still exposes stable metadata that allows users
 * to identify and use the stream.
 *
 * @param videomaster_context VideoMasterContext pointer to the context
 * @param board_name Name of the board
 * @param serial_number Board serial number
 * @return char* Formatted fallback description string
 */
static char *
format_fallback_device_description(VideoMasterContext *videomaster_context,
                                   const char         *board_name,
                                   const char         *serial_number);

/**
 * @brief  Formats the device name string.
 *
 * This function formats the device name string using the provided board name
 * and serial number.
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param board_name  Name of the board
 * @param serial_number  Serial number of the board
 * @return char*  Formatted device name string
 */
static char *format_device_name(VideoMasterContext *videomaster_context,
                                const char         *board_name,
                                const char         *serial_number);
/**
 * @brief Get the active loopback property for a given channel index
 *
 * @param channel_index  Index of the channel
 * @return VHD_CORE_BOARDPROPERTY  The active loopback property for the channel
 */
static VHD_CORE_BOARDPROPERTY get_active_loopback_property(int channel_index);

/**
 * @brief Get the audio buffer for the device and
 * channel set in videomaster_context.
 *
 * The audio buffer is allocated outside VideoMaster API and should be release
 * after its usage. The  pointer is stored in the videomaster_context context.
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @return int 0 on success, negative AVERROR code on failure
 */
static int get_audio_buffer(VideoMasterContext *videomaster_context);

/**
 * @brief Get the board name from the device identify by the
 * board_index stored in the context
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param board_index  Index of the board
 * @param board_name  Pointer to the variable to store the board name
 * @return int 0 on success, negative AVERROR code on failure
 */
static int get_board_name(VideoMasterContext *videomaster_context,
                          uint32_t board_index, char **board_name);

/**
 * @brief Get the board name and serial number from the device identify by the
 * board_index stored in the context
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param board_name  Pointer to the variable to store the board name
 * @param serial_number  Pointer to the variable to store the serial number
 * @return int 0 on success, negative AVERROR code on failure
 */
static int
get_board_name_and_serial_number(VideoMasterContext *videomaster_context,
                                 char **board_name, char **serial_number);

/**
 * @brief Get the buffer packing info object
 *
 * @param packing AVVideoMasterBufferPacking for whom the info is requested
 * @return const BufferPackingInfo*
 */
static const VideoMasterBufferPackingInfo *
get_buffer_packing_info(enum AVVideoMasterBufferPacking packing);

/**
 * @brief Get the channel binary mask from the number
 * of channel
 *
 * @param videomaster_context  VideoMasterContext
 * pointer to the VideoMasterContext
 * @return int
 */
static int
get_channel_mask_from_nb_channels(VideoMasterContext *videomaster_context);

/**
 * @brief Get the firmware loopback property for a given channel index
 *
 * @param channel_index  Index of the channel
 * @return VHD_CORE_BOARDPROPERTY  The firmware loopback property for the
 * channel
 */
static VHD_CORE_BOARDPROPERTY get_firmware_loopback_property(int channel_index);

/**
 * @brief Get the passive loopback property for a given channel index
 *
 * @param channel_index  Index of the channel
 * @return VHD_CORE_BOARDPROPERTY  The passive loopback property for the
 * channel
 */
static VHD_CORE_BOARDPROPERTY get_passive_loopback_property(int channel_index);

/**
 * @brief Get the rx stream type Videomaster enumeration from the  channel index
 *
 * @param index Channel Index
 * @return VHD_STREAMTYPE
 */
/**
 * @brief Get the serial_number from the device identify by the
 * board_index stored in the context
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param board_index  Index of the board
 * @param serial_number  Pointer to the variable to store the serial_number
 * @return int 0 on success, negative AVERROR code on failure
 */
static int get_serial_number(VideoMasterContext *videomaster_context,
                             HANDLE board_handle, char **serial_number);

/**
 * @brief Get the video buffer for the device and
 * channel set in videomaster_context.
 *
 * The video buffer is allocated and managed by the VideoMaster API and the
 * pointer is stored in the videomaster_context context.
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @return int 0 on success, negative AVERROR code on failure
 */
static int get_video_buffer(VideoMasterContext *videomaster_context);

/**
 * @brief Get the videomaster enumeration value for timestamp source object
 *
 * @param type AVVideoMasterTimeStampType type
 * @return int The videomaster enumeration value for timestamp source
 */
static int get_videomaster_enumeration_value_for_timestamp_source(
    enum AVVideoMasterTimeStampType type);

/**
 * @brief Lock the VideoMaster slot for audio and video data for the device and
 * channel set in videomaster_context.
 *
 * The slot handle is created in the videomaster_context object.
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @return int 0 on success, negative AVERROR code on failure
 */
static int lock_slot(VideoMasterContext *videomaster_context);

/**
 * @brief Unlock the VideoMaster slot for audio and video data for the device
 * and channel set in videomaster_context.
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @return int 0 on success, negative AVERROR code on failure
 */
static int unlock_slot(VideoMasterContext *videomaster_context);

/**
 * @brief Release the audio info structure
 * This function releases the audio info structure
 * and frees the memory allocated for it.
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param audio_info  Pointer to the variable to store the audio info
 * @return int 0 on success, negative AVERROR code on failure
 */
static int release_audio_info(VideoMasterContext *videomaster_context,
                              VHD_AUDIOINFO      *audio_info);

/** static functions definitions **/
static int add_device_info_into_list(VideoMasterContext *videomaster_context,
                                     char *board_name, char *serial_number,
                                     struct AVDeviceInfoList **device_list)
{
    int                             av_error = 0;
    char                           *device_name = NULL;
    char                           *device_description = NULL;
    char                           *enriched_description = NULL;
    AVDeviceInfo                   *new_device = NULL;
    enum AVVideoMasterBufferPacking buffer_packing;

    videomaster_context->channel_type =
        ff_videomaster_get_channel_type_from_index(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->channel_index);

    device_name = format_device_name(videomaster_context, board_name,
                                     serial_number);

    device_description = format_fallback_device_description(videomaster_context,
                                                            board_name,
                                                            serial_number);

    if (!device_name || !device_description)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for device name or description for "
               "channel %u on board %u\n",
               videomaster_context->channel_index,
               videomaster_context->board_index);
        av_freep(&device_name);
        av_freep(&device_description);
        return AVERROR(ENOMEM);
    }

    av_error = ff_videomaster_get_video_stream_properties(
        videomaster_context->avctx, videomaster_context->board_handle,
        videomaster_context->stream_handle, videomaster_context->channel_index,
        &videomaster_context->channel_type, &videomaster_context->video_info,
        &videomaster_context->video_width, &videomaster_context->video_height,
        &videomaster_context->video_frame_rate_num,
        &videomaster_context->video_frame_rate_den,
        &videomaster_context->video_interlaced,
        videomaster_context->dual_stream);

    if (av_error == 0)
    {
        buffer_packing = AV_VIDEOMASTER_BUFFER_PACKING_YUV422_10;

        if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
            buffer_packing =
                ff_videomaster_get_buffer_packing_from_cable_bit_sampling_hdmi(
                    videomaster_context->video_info.hdmi.cable_bit_sampling);
        else if (videomaster_context->channel_type ==
                 AV_VIDEOMASTER_CHANNEL_IP_2110)
        {
            if (videomaster_context->ip_video_depth == 8)
                buffer_packing = AV_VIDEOMASTER_BUFFER_PACKING_YUV422_8;
        }

        if (ff_videomaster_get_audio_stream_properties(
                videomaster_context->avctx, videomaster_context->board_handle,
                videomaster_context->stream_handle,
                videomaster_context->channel_index, buffer_packing,
                &videomaster_context->channel_type,
                &videomaster_context->audio_info,
                &videomaster_context->audio_sample_rate,
                &videomaster_context->audio_nb_channels,
                &videomaster_context->audio_sample_size,
                &videomaster_context->audio_codec) != 0)
        {
            av_log(videomaster_context->avctx, AV_LOG_DEBUG,
                   "Detailed audio properties are unavailable for channel %u "
                   "on board %u\n",
                   videomaster_context->channel_index,
                   videomaster_context->board_index);
        }

        enriched_description = format_device_description(videomaster_context,
                                                         board_name,
                                                         serial_number);
        if (enriched_description)
        {
            av_freep(&device_description);
            device_description = enriched_description;
        }
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "Detailed properties unavailable for channel %u on board %u. "
               "Keeping fallback description.\n",
               videomaster_context->channel_index,
               videomaster_context->board_index);
    }

    new_device = create_device_info(videomaster_context, device_name,
                                    device_description, true);

    if (!new_device)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to create device info for channel %u on "
               "board %u\n",
               videomaster_context->channel_index,
               videomaster_context->board_index);
        av_freep(&device_name);
        av_freep(&device_description);
        return AVERROR(ENOMEM);
    }

    av_log(videomaster_context->avctx, AV_LOG_DEBUG,
           "Device info created for channel %u on board %u : device_name = "
           "%s, device_description = %s\n",
           videomaster_context->channel_index, videomaster_context->board_index,
           device_name, device_description);

    if (new_device &&
        av_dynarray_add_nofree(&(*device_list)->devices,
                               &(*device_list)->nb_devices, new_device) < 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to add device info to list\n");
        av_freep(&new_device->device_name);
        av_freep(&new_device->device_description);
        av_freep(&new_device);
        return AVERROR(ENOMEM);
    }

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Device info for channel %u on board %u added to list\n",
           videomaster_context->channel_index,
           videomaster_context->board_index);

    return 0;
}

static AVDeviceInfo *create_device_info(VideoMasterContext *videomaster_context,
                                        char               *device_name,
                                        char *device_description, bool is_video)
{
    AVDeviceInfo *device_info = av_mallocz(sizeof(AVDeviceInfo));
    if (!device_info)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for device info\n");
        return NULL;
    }
    device_info->device_name = device_name;
    device_info->device_description = device_description;
    device_info->media_types = av_mallocz(sizeof(enum AVMediaType) * 2);
    if (!device_info->media_types)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for media types\n");
        av_freep(&device_info);
        device_info = NULL;
    }

    // HDMI devices are always video devices and have always audio (muted or
    // not)
    device_info->media_types[0] = AVMEDIA_TYPE_VIDEO;
    device_info->media_types[1] = AVMEDIA_TYPE_AUDIO;
    device_info->nb_media_types = 2;

    return device_info;
}

static VHD_CORE_BOARDPROPERTY
select_loopback_property(VideoMasterContext *videomaster_context)
{
    uint32_t has_firmware_loopback = false;
    uint32_t has_active_loopback = false;
    uint32_t has_passive_loopback = false;
    int      channel_index = videomaster_context->channel_index;

    if (VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_FIRMWARE_LOOPBACK,
                               &has_firmware_loopback) != VHDERR_NOERROR)
        has_firmware_loopback = false;
    if (VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_ACTIVE_LOOPBACK,
                               &has_active_loopback) != VHDERR_NOERROR)
        has_active_loopback = false;
    if (VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_PASSIVE_LOOPBACK,
                               &has_passive_loopback) != VHDERR_NOERROR)
        has_passive_loopback = false;

    if (has_firmware_loopback &&
        get_firmware_loopback_property(channel_index) !=
            NB_VHD_CORE_BOARDPROPERTIES)
        return get_firmware_loopback_property(channel_index);

    if (has_active_loopback && get_active_loopback_property(channel_index) !=
                                   NB_VHD_CORE_BOARDPROPERTIES)
        return get_active_loopback_property(channel_index);

    if (has_passive_loopback && get_passive_loopback_property(channel_index) !=
                                    NB_VHD_CORE_BOARDPROPERTIES)
        return get_passive_loopback_property(channel_index);

    return NB_VHD_CORE_BOARDPROPERTIES;
}

static int disable_loopback_on_channel(VideoMasterContext *videomaster_context)
{
    VHD_CORE_BOARDPROPERTY property;
    uint32_t               state = false;
    int                    av_error;

    /* Already saved by an earlier start not followed by a stop: the current
     * board state is ours, so it must not overwrite the original one. */
    if (videomaster_context->loopback_saved)
        return 0;

    property = select_loopback_property(videomaster_context);
    if (property == NB_VHD_CORE_BOARDPROPERTIES)
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "No loopback available on channel %u\n",
               videomaster_context->channel_index);
        return 0;
    }

    av_error = ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_GetBoardProperty(videomaster_context->board_handle, property,
                             &state),
        "Loopback state retrieved successfully",
        "Failed to retrieve loopback state");
    if (av_error != 0)
        return av_error;

    videomaster_context->loopback_property = property;
    videomaster_context->loopback_original_state = state;
    videomaster_context->loopback_saved = true;

    if (!state)
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Loopback already disabled on channel %u, left untouched\n",
               videomaster_context->channel_index);
        return 0;
    }

    av_error = ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetBoardProperty(videomaster_context->board_handle, property,
                             false),
        "Loopback disabled successfully", "Failed to disable loopback");
    if (av_error != 0)
        return av_error;

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Loopback disabled on channel %u (board property %d, original "
           "state: %u)\n",
           videomaster_context->channel_index, (int)property, state);

    return 0;
}

static int restore_loopback_on_channel(VideoMasterContext *videomaster_context)
{
    uint32_t state = false;
    int      av_error;

    if (!videomaster_context->loopback_saved)
        return 0;

    videomaster_context->loopback_saved = false;

    if (VHD_GetBoardProperty(videomaster_context->board_handle,
                             videomaster_context->loopback_property,
                             &state) == VHDERR_NOERROR &&
        state == videomaster_context->loopback_original_state)
        return 0;

    av_error = ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetBoardProperty(videomaster_context->board_handle,
                             videomaster_context->loopback_property,
                             videomaster_context->loopback_original_state),
        "Loopback restored successfully", "Failed to restore loopback");
    if (av_error != 0)
        return av_error;

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Loopback restored to original state %u on channel %u\n",
           videomaster_context->loopback_original_state,
           videomaster_context->channel_index);

    return 0;
}

int ff_videomaster_disable_loopback(VideoMasterContext *videomaster_context)
{
    enum AVVideoMasterChannelType channel_type;
    int                           av_error;

    channel_type = ff_videomaster_get_channel_type_from_index(
        videomaster_context->avctx, videomaster_context->board_handle,
        videomaster_context->channel_index);
    if (channel_type != AV_VIDEOMASTER_CHANNEL_SDI &&
        channel_type != AV_VIDEOMASTER_CHANNEL_ASISDI &&
        channel_type != AV_VIDEOMASTER_CHANNEL_HDMI)
        return 0;

    av_error = disable_loopback_on_channel(videomaster_context);
    if (av_error != 0)
        return av_error;

    /* The input signal only reaches the receiver once the loopback is
     * released: give the channel some time to lock. Not needed if the
     * loopback was already disabled. */
    if (!videomaster_context->loopback_saved ||
        !videomaster_context->loopback_original_state)
        return 0;

    for (int elapsed_ms = 0; elapsed_ms < VIDEOMASTER_LOOPBACK_LOCK_TIMEOUT_MS;
         elapsed_ms += VIDEOMASTER_LOOPBACK_LOCK_POLL_MS)
    {
        if (ff_videomaster_is_channel_locked(videomaster_context))
        {
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "Channel %u locked %d ms after loopback was disabled\n",
                   videomaster_context->channel_index, elapsed_ms);
            return 0;
        }
        av_usleep(VIDEOMASTER_LOOPBACK_LOCK_POLL_MS * 1000);
    }

    {
        uint32_t channel_status = 0;
        uint32_t loopback_state = 0;
        VHD_GetChannelProperty(videomaster_context->board_handle,
                               VHD_RX_CHANNEL,
                               videomaster_context->channel_index,
                               VHD_CORE_CP_STATUS, &channel_status);
        VHD_GetBoardProperty(videomaster_context->board_handle,
                             videomaster_context->loopback_property,
                             &loopback_state);
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Channel %u still not locked %d ms after loopback was disabled "
               "(channel status: 0x%08X, loopback state read back: %u)\n",
               videomaster_context->channel_index,
               VIDEOMASTER_LOOPBACK_LOCK_TIMEOUT_MS, channel_status,
               loopback_state);
    }

    return 0;
}

/* Helper functions for ff_videomaster_start_stream */

static int setup_field_merge(VideoMasterContext *videomaster_context)
{
    int av_error = 0;
    int has_field_merge_capability = 0;

    if (!videomaster_context->video_interlaced)
    {
        videomaster_context->video_needs_field_reorder = false;
        return 0;
    }

    videomaster_context->video_needs_field_reorder = false;

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_GetBoardCapability(videomaster_context->board_handle,
                                         VHD_CORE_BOARD_CAP_FIELD_MERGING,
                                         &has_field_merge_capability),
                  "", "");
    if (has_field_merge_capability)
    {
        GET_AND_CHECK(
            ff_videomaster_handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_CORE_SP_FIELD_MERGE, true),
            "", "Unable to set field merge property for interlaced stream");
    }
    else
    {
        /* Keep frame-mode output for FFmpeg interlaced semantics. Some
         * configurations expose fields as top-half then bottom-half in the
         * frame buffer; mark this case for packet-time reordering. */
        if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_IP_2110)
            videomaster_context->video_needs_field_reorder = true;

        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Field merge unavailable. Keeping frame mode; interlaced "
               "buffers will be reordered to line-interleaved layout.\n");
    }
    return 0;
}

static int setup_transfer_scheme(VideoMasterContext *videomaster_context)
{
    if (videomaster_context->has_video)
        ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_CORE_SP_TRANSFER_SCHEME,
                                  VHD_TRANSFER_SLAVED),
            "", "");

    /* A StreamSync requires every member stream to use this transfer
     * scheme, not just the main (video) one. */
    if (videomaster_context->ip_sync_mode)
        ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->ip_audio_stream_handle,
                                  VHD_CORE_SP_TRANSFER_SCHEME,
                                  VHD_TRANSFER_SLAVED),
            "", "");
    return 0;
}

static int setup_buffer_packing(VideoMasterContext *videomaster_context)
{
    int                                 av_error = 0;
    const VideoMasterBufferPackingInfo *info = NULL;
    bool                                has_line_padding = false;

    if (videomaster_context->video_buffer_packing ==
        AV_NB_VIDEOMASTER_BUFFER_PACKINGS)
    {
        has_line_padding =
            (VHD_SetStreamProperty(videomaster_context->stream_handle,
                                   VHD_CORE_SP_LINE_PADDING,
                                   128) == VHDERR_INVALIDPROPERTY);
        info = get_buffer_packing_info(
            has_line_padding ? AV_VIDEOMASTER_BUFFER_PACKING_YUV422_10
                             : AV_VIDEOMASTER_BUFFER_PACKING_YUV422_8);
    }
    else
    {
        info = get_buffer_packing_info(
            videomaster_context->video_buffer_packing);
    }

    if (!info)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Not implemented video buffer packing: %s\n",
               VHD_BUFFERPACKING_ToPrettyString(
                   videomaster_context->video_buffer_packing));
        return AVERROR(EINVAL);
    }

    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(videomaster_context->stream_handle,
                              VHD_CORE_SP_BUFFER_PACKING, info->buffer_packing),
        "", "");

    if (info->codec_or_pixel_format)
        videomaster_context->video_codec = info->format.codec_id;
    else
    {
        videomaster_context->video_codec = AV_CODEC_ID_RAWVIDEO;
        videomaster_context->video_pixel_format = info->format.pixel_format;
    }

    videomaster_context->video_bit_rate = av_rescale(
        videomaster_context->video_width * videomaster_context->video_height *
            info->bits_per_pixel,
        videomaster_context->video_frame_rate_num * info->bit_rate_num_mul,
        videomaster_context->video_frame_rate_den * info->bit_rate_den_mul);

    if (info->line_padding_needed &&
        VHD_SetStreamProperty(videomaster_context->stream_handle,
                              VHD_CORE_SP_LINE_PADDING,
                              128) == VHDERR_INVALIDPROPERTY)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board does not support line padding property. Unable "
               "to select %s video buffer packing explicitly. Leave the "
               "option unset or select another buffer packing.\n",
               VHD_BUFFERPACKING_ToPrettyString(info->buffer_packing));
        return AVERROR(EINVAL);
    }

    av_log(videomaster_context->avctx, AV_LOG_INFO,
           "Video buffer packing selected: %s\n",
           VHD_BUFFERPACKING_ToPrettyString(info->buffer_packing));

    return 0;
}

static int setup_timestamp_source(VideoMasterContext *videomaster_context)
{
    int  av_error = 0;
    bool audio_source_applies = videomaster_context->channel_type ==
                                    AV_VIDEOMASTER_CHANNEL_IP_2110 &&
                                videomaster_context->has_audio;
    /* osc/system share a single board-wide clock type property; whichever
     * essence needs it is fine to apply since check_timestamp_source()
     * already rejected the two disagreeing. */
    bool need_board_clk_type = videomaster_context->timestamp_source <
                                   AV_VIDEOMASTER_TIMESTAMP_HARDWARE ||
                               (audio_source_applies &&
                                videomaster_context->audio_timestamp_source <
                                    AV_VIDEOMASTER_TIMESTAMP_HARDWARE);
    enum AVVideoMasterTimeStampType board_clk_source =
        videomaster_context->timestamp_source <
                AV_VIDEOMASTER_TIMESTAMP_HARDWARE
            ? videomaster_context->timestamp_source
            : videomaster_context->audio_timestamp_source;

    if (need_board_clk_type)
        GET_AND_CHECK(
            ff_videomaster_handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_SetBoardProperty(
                videomaster_context->board_handle,
                VHD_CORE_BP_SYSTEM_TIME_CLK_TYPE,
                get_videomaster_enumeration_value_for_timestamp_source(
                    board_clk_source)),
            "System time clock type set successfully",
            "Failed to set system time clock type");

    if (videomaster_context->timestamp_source ==
            AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD ||
        (audio_source_applies && videomaster_context->audio_timestamp_source ==
                                     AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD))
    {
        ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetBoardProperty(videomaster_context->board_handle,
                                 VHD_SDI_BP_BLACKBURST0_DETECTION_ENABLE,
                                 FALSE),
            "Disabled Blackburst detection for LTC on-board signal",
            "Failed to disable Blackburst detection for LTC on-board signal");
    }

    return 0;
}

static char *format_device_description(VideoMasterContext *videomaster_context,
                                       const char         *board_name,
                                       const char         *serial_number)
{
    char *device_description = av_mallocz(256);
    if (!device_description)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for device description\n");
        return NULL;
    }

    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
        ff_videomaster_format_channel_description_hdmi(videomaster_context,
                                                       board_name,
                                                       serial_number,
                                                       device_description, 256);
    else if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_SDI ||
             videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI)
        ff_videomaster_format_channel_description_sdi(videomaster_context,
                                                      board_name, serial_number,
                                                      device_description, 256);
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Detailed description formatting not implemented for channel "
               "type %s. Using fallback description.\n",
               ff_videomaster_channel_type_to_string(
                   videomaster_context->channel_type));
        device_description = format_fallback_device_description(
            videomaster_context, board_name, serial_number);
    }

    return device_description;
}

static char *
format_fallback_device_description(VideoMasterContext *videomaster_context,
                                   const char         *board_name,
                                   const char         *serial_number)
{
    char       *device_description = av_mallocz(256);
    const char *channel_type = ff_videomaster_channel_type_to_string(
        videomaster_context->channel_type);
    const char *status;

    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_IP_2110)
        status = ff_videomaster_get_channel_status_ip(videomaster_context);
    else
        status = "locked";

    if (!device_description)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for fallback device description\n");
        return NULL;
    }

    snprintf(device_description, 256,
             "%s channel %u (%s) on board %u (%s, SN: %s)", channel_type,
             videomaster_context->channel_index, status,
             videomaster_context->board_index,
             board_name ? board_name : "unknown board",
             serial_number ? serial_number : "unknown");

    return device_description;
}

static char *format_device_name(VideoMasterContext *videomaster_context,
                                const char         *board_name,
                                const char         *serial_number)
{
    char *device_name = av_mallocz(256);
    if (!device_name)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for device name\n");
        return NULL;
    }
    snprintf(device_name, 256, "stream %u on board %u",
             videomaster_context->channel_index,
             videomaster_context->board_index);
    return device_name;
}

const char *ff_videomaster_channel_type_to_string(
    enum AVVideoMasterChannelType channel_type)
{
    switch (channel_type)
    {
    case AV_VIDEOMASTER_CHANNEL_HDMI:
        return "HDMI";
    case AV_VIDEOMASTER_CHANNEL_SDI:
        return "SDI";
    case AV_VIDEOMASTER_CHANNEL_ASISDI:
        return "ASI/SDI";
    case AV_VIDEOMASTER_CHANNEL_IP_2110:
        return "IP 2110";
    default:
        return "Unknown";
    }
}

static VHD_CORE_BOARDPROPERTY get_active_loopback_property(int channel_index)
{
    switch (channel_index)
    {
    case 0:
        return VHD_CORE_BP_ACTIVE_LOOPBACK_0;
    default:
        return NB_VHD_CORE_BOARDPROPERTIES;
    }
}

static int get_audio_buffer(VideoMasterContext *videomaster_context)
{
    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {
        return ff_videomaster_get_audio_buffer_hdmi(
            videomaster_context,
            get_channel_mask_from_nb_channels(videomaster_context));
    }
    else if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_SDI ||
             videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI)
        return ff_videomaster_get_audio_buffer_sdi(videomaster_context);
    else if (videomaster_context->channel_type ==
             AV_VIDEOMASTER_CHANNEL_IP_2110)
    {
        /* Deliberate no-op: unlike HDMI/SDI embedded audio (extracted from
         * the single shared slot locked above), IP ST2110-30 audio is
         * always a separate essence with its own stream/slot handle,
         * fetched independently by the caller (see
         * ff_videomaster_lock_next_slot_ip() in videomaster_ip.c). This
         * function must not treat that as an error. */
        return 0;
    }

    av_log(videomaster_context->avctx, AV_LOG_ERROR,
           "Audio buffer extraction is not supported for channel type %s\n",
           ff_videomaster_channel_type_to_string(
               videomaster_context->channel_type));
    return AVERROR(ENOSYS);
}

static int
get_board_name_and_serial_number(VideoMasterContext *videomaster_context,
                                 char **board_name, char **serial_number)
{
    int av_error = 0;

    GET_AND_CHECK(get_board_name, videomaster_context->avctx,
                  videomaster_context, videomaster_context->board_index,
                  board_name);

    GET_AND_CHECK(get_serial_number, videomaster_context->avctx,
                  videomaster_context, videomaster_context->board_handle,
                  serial_number);

    av_log(videomaster_context->avctx, AV_LOG_TRACE, "Board name: %s\n",
           *board_name);
    av_log(videomaster_context->avctx, AV_LOG_TRACE, "Serial number: %s\n",
           *serial_number);

    return 0;
}

static int get_board_name(VideoMasterContext *videomaster_context,
                          uint32_t board_index, char **board_name)
{
    const char *local_board_name = VHD_GetBoardModel(board_index);
    *board_name = av_strdup(local_board_name);
    if (!*board_name)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for board name\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

static const VideoMasterBufferPackingInfo *
get_buffer_packing_info(enum AVVideoMasterBufferPacking packing)
{
    for (size_t i = 0; i < sizeof(buffer_packing_info_table) /
                               sizeof(buffer_packing_info_table[0]);
         ++i)
    {
        if (buffer_packing_info_table[i].buffer_packing == packing)
            return &buffer_packing_info_table[i];
    }
    return NULL;
}

static int
get_channel_mask_from_nb_channels(VideoMasterContext *videomaster_context)
{
    switch (videomaster_context->audio_nb_channels)
    {
    case 0:
        return 0b00000000;
    case 1:
        return 0b00000001;
    case 2:
        return 0b00000011;
    case 3:
        return 0b00000111;
    case 4:
        return 0b00001111;
    case 5:
        return 0b00011111;
    case 6:
        return 0b00111111;
    case 7:
        return 0b01111111;
    case 8:
        return 0b11111111;
    default:
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Unsupported number of channels: %u\n",
               videomaster_context->audio_nb_channels);
        return AVERROR(EINVAL);
    }
}

static VHD_CORE_BOARDPROPERTY get_firmware_loopback_property(int channel_index)
{
    switch (channel_index)
    {
    case 0:
        return VHD_CORE_BP_FIRMWARE_LOOPBACK_0;
    case 1:
        return VHD_CORE_BP_FIRMWARE_LOOPBACK_1;
    default:
        return NB_VHD_CORE_BOARDPROPERTIES;
    }
}

VHD_STREAMTYPE
ff_videomaster_get_rx_stream_type_from_index(uint32_t index)
{
    switch (index)
    {
    case 0:
        return VHD_ST_RX0;
    case 1:
        return VHD_ST_RX1;
    case 2:
        return VHD_ST_RX2;
    case 3:
        return VHD_ST_RX3;
    case 4:
        return VHD_ST_RX4;
    case 5:
        return VHD_ST_RX5;
    case 6:
        return VHD_ST_RX6;
    case 7:
        return VHD_ST_RX7;
    case 8:
        return VHD_ST_RX8;
    case 9:
        return VHD_ST_RX9;
    case 10:
        return VHD_ST_RX10;
    case 11:
        return VHD_ST_RX11;
    default:
        return NB_VHD_STREAMTYPES;
    }
}

static VHD_CORE_BOARDPROPERTY get_passive_loopback_property(int channel_index)
{
    switch (channel_index)
    {
    case 0:
        return VHD_CORE_BP_BYPASS_RELAY_0;
    case 1:
        return VHD_CORE_BP_BYPASS_RELAY_1;
    case 2:
        return VHD_CORE_BP_BYPASS_RELAY_2;
    case 3:
        return VHD_CORE_BP_BYPASS_RELAY_3;
    default:
        return NB_VHD_CORE_BOARDPROPERTIES;
    }
}
static int get_serial_number(VideoMasterContext *videomaster_context,
                             HANDLE board_handle, char **serial_number)
{
    uint32_t       serial_number_array[4] = { 0, 0, 0, 0 };
    const uint32_t properties[] = { VHD_CORE_BP_SERIALNUMBER_PART1_LSW,
                                    VHD_CORE_BP_SERIALNUMBER_PART2,
                                    VHD_CORE_BP_SERIALNUMBER_PART3,
                                    VHD_CORE_BP_SERIALNUMBER_PART4_MSW };
    *serial_number = av_mallocz(32);
    if (!*serial_number)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for serial number string\n");
        return AVERROR(ENOMEM);
    }

    for (int i = 0; i < sizeof(properties) / sizeof(properties[0]); i++)
        if (ff_videomaster_handle_vhd_status(
                videomaster_context->avctx,
                VHD_GetBoardProperty(board_handle, properties[i],
                                     &serial_number_array[i]),
                "Serial number part retrieved successfully",
                "Failed to retrieve serial number part") != 0)
        {
            av_log(videomaster_context->avctx, AV_LOG_WARNING,
                   "Failed to retrieve serial number part %d\n", i);
            snprintf(*serial_number, 32, "");
            return 0;
        }

    if (snprintf(*serial_number, 32, "%08X%08X%08X%08X", serial_number_array[0],
                 serial_number_array[1], serial_number_array[2],
                 serial_number_array[3]) < 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Failed to format serial number string\n");
        snprintf(*serial_number, 32, "");
    }

    return 0;
}

static int get_video_buffer(VideoMasterContext *videomaster_context)
{
    uint32_t video_buffer_type = ff_videomaster_get_video_buffer_type_sdi();

    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {
        video_buffer_type = ff_videomaster_get_video_buffer_type_hdmi();
    }
    else if (videomaster_context->channel_type ==
             AV_VIDEOMASTER_CHANNEL_IP_2110)
    {
        video_buffer_type = ff_videomaster_get_video_buffer_type_ip();
    }
    else if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_SDI ||
             videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI)
    {
        video_buffer_type = ff_videomaster_get_video_buffer_type_sdi();
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Video buffer extraction is not supported for channel type %s\n",
               ff_videomaster_channel_type_to_string(
                   videomaster_context->channel_type));
        return AVERROR(ENOSYS);
    }
    return ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_GetSlotBuffer(videomaster_context->slot_handle, video_buffer_type,
                          &videomaster_context->video_buffer,
                          &videomaster_context->video_buffer_size),
        "Video slot buffer retrieved successfully",
        "Failed to retrieve video slot buffer");
}

static int get_videomaster_enumeration_value_for_timestamp_source(
    enum AVVideoMasterTimeStampType type)
{
    switch (type)
    {
    case AV_VIDEOMASTER_TIMESTAMP_OSCILLATOR:
        return VHD_ST_CLK_TYPE_MONOTONIC_RAW;
    case AV_VIDEOMASTER_TIMESTAMP_SYSTEM:
        return VHD_ST_CLK_TYPE_REALTIME;
    case AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD:
        return VHD_TC_SRC_LTC_ONBOARD;
    case AV_VIDEOMASTER_TIMESTAMP_LTC_COMPANION_CARD:
        return VHD_TC_SRC_LTC_COMPANION_CARD;
    default:
        return -1;
    }
}

int ff_videomaster_handle_av_error(AVFormatContext *avctx, int av_error,
                                   const char *trace_message,
                                   const char *error_message)
{
    if (av_error == 0 && strcmp(trace_message, "") != 0)
    {
        av_log(avctx, AV_LOG_TRACE, "%s\n", trace_message);
    }
    else if (strcmp(trace_message, "") != 0)
    {
        av_log(avctx, AV_LOG_ERROR, "%s\n", error_message);
    }
    return av_error;
}

int ff_videomaster_handle_vhd_status(AVFormatContext *avctx,
                                     VHD_ERRORCODE    vhd_status,
                                     const char      *success_message,
                                     const char      *error_message)
{
    if (vhd_status == VHDERR_NOERROR && strcmp(success_message, "") != 0)
    {
        av_log(avctx, AV_LOG_TRACE, "%s.\n", success_message);
    }
    else if (vhd_status == VHDERR_TIMEOUT)
    {
        return AVERROR(EAGAIN);
    }
    else if (vhd_status != VHDERR_NOERROR)
    {
        char pLastErrorMessage[VHD_MAX_ERROR_STRING_SIZE] = { 0 };
        VHD_GetLastErrorMessage(pLastErrorMessage, VHD_MAX_ERROR_STRING_SIZE);
        av_log(avctx, AV_LOG_DEBUG, "VHDERR = %d - %s\n%s\n", vhd_status,
               VHD_ERRORCODE_ToPrettyString(vhd_status), pLastErrorMessage);
        if (strcmp(error_message, "") != 0)
            av_log(avctx, AV_LOG_ERROR, "%s.\n", error_message);
        return AVERROR(EIO);
    }
    return 0;
}

static int lock_slot(VideoMasterContext *videomaster_context)
{
    return ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_LockSlotHandle(videomaster_context->stream_handle,
                           &videomaster_context->slot_handle),
        "Slot handle locked successfully", "Failed to lock slot handle");
}

static int unlock_slot(VideoMasterContext *videomaster_context)
{
    int return_code = 0;
    if (videomaster_context->slot_handle)
    {
        return_code = ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_UnlockSlotHandle(videomaster_context->slot_handle),
            "Slot handle unlocked successfully",
            "Failed to unlock slot handle");
        videomaster_context->slot_handle = NULL;
    }
    return return_code;
}

static int release_audio_info(VideoMasterContext *videomaster_context,
                              VHD_AUDIOINFO      *audio_info)
{
    if (audio_info == NULL)
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Audio info is NULL, nothing to release\n");
        return 0;
    }

    if (!videomaster_context->has_audio)
        return 0;

    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_SDI ||
        videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI)
        return ff_videomaster_release_audio_info_sdi(videomaster_context,
                                                     audio_info);

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "No VHD_AUDIOINFO buffers to release for channel type %s\n",
           ff_videomaster_channel_type_to_string(
               videomaster_context->channel_type));
    return 0;
}

/** functions definitions */

int ff_videomaster_close_board_handle(VideoMasterContext *videomaster_context)
{
    int return_code;

    restore_loopback_on_channel(videomaster_context);

    return_code = ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_CloseBoardHandle(videomaster_context->board_handle),
        "Board handle closed successfully", "Failed to close board handle");
    videomaster_context->board_handle = NULL;
    return return_code;
}

int ff_videomaster_close_stream_handle(VideoMasterContext *videomaster_context)
{
    int return_code = ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_CloseStreamHandle(videomaster_context->stream_handle),
        "Stream handle closed successfully", "Failed to close stream handle");
    videomaster_context->stream_handle = NULL;
    return return_code;
}

int ff_videomaster_create_devices_infos_from_board_index(
    VideoMasterContext *videomaster_context, uint32_t board_index,
    struct AVDeviceInfoList **device_list)
{
    char *board_name = NULL;
    char *serial_number = NULL;
    int   av_error = 0;

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_create_devices_"
           "infos_from_board_index: IN\n");
    videomaster_context->board_index = board_index;

    GET_AND_CHECK(ff_videomaster_open_board_handle, videomaster_context->avctx,
                  videomaster_context);

    av_error = ff_videomaster_handle_av_error(
        videomaster_context->avctx,
        get_board_name_and_serial_number(videomaster_context, &board_name,
                                         &serial_number),
        "Board name and serial number "
        "retrieved successfully",
        "Failed to retrieve board name and "
        "serial number");

    if (av_error != 0)
    {
        ff_videomaster_close_board_handle(videomaster_context);

        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "ff_videomaster_create_devices_"
               "infos_from_board_index: OUT\n");
        return av_error;
    }

    av_error = ff_videomaster_get_nb_rx_channels(videomaster_context);
    if (av_error != 0)
    {
        ff_videomaster_close_board_handle(videomaster_context);
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "ff_videomaster_create_devices_"
               "infos_from_board_index: OUT\n");
        return av_error;
    }

    for (uint32_t channel_index = 0;
         channel_index < videomaster_context->nb_rx_channels; channel_index++)
    {
        videomaster_context->channel_index = channel_index;

        if (ff_videomaster_disable_loopback(videomaster_context) != 0)
            av_log(videomaster_context->avctx, AV_LOG_WARNING,
                   "Failed to disable loopback on channel %u of board %u\n",
                   channel_index, board_index);

        if (ff_videomaster_is_channel_locked(videomaster_context))
        {
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "Channel %u is locked on "
                   "board %u -> create "
                   "device info\n",
                   channel_index, board_index);

            av_error = add_device_info_into_list(videomaster_context,
                                                 board_name, serial_number,
                                                 device_list);
        }
        else
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "Channel %u is unlocked "
                   "on board %u\n",
                   channel_index, board_index);

        restore_loopback_on_channel(videomaster_context);

        if (av_error != 0)
            break;
    }
    ff_videomaster_close_board_handle(videomaster_context);
    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_create_devices_"
           "infos_from_board_index: OUT\n");

    return av_error;
}

int ff_videomaster_extract_context(AVFormatContext     *avctx,
                                   VideoMasterData    **videomaster_data,
                                   VideoMasterContext **videomaster_context)
{
    av_log(avctx, AV_LOG_TRACE,
           "ff_videomaster_extract_context: "
           "IN\n");
    if (!avctx)
    {
        av_log(avctx, AV_LOG_ERROR, "avctx is NULL!\n");
        av_log(avctx, AV_LOG_TRACE,
               "ff_videomaster_extract_"
               "context: OUT\n");
        return AVERROR(EINVAL);
    }
    *videomaster_data = (struct VideoMasterData *)avctx->priv_data;
    *videomaster_context =
        (struct VideoMasterContext *)(*videomaster_data)->context;
    if (!(*videomaster_context))
    {
        av_log(avctx, AV_LOG_DEBUG,
               "videomaster_context is NULL. "
               "Allocate new context.\n");
        *videomaster_context = av_mallocz(sizeof(struct VideoMasterContext));
        if (!(*videomaster_context))
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to allocate memory "
                   "for videomaster_context\n");
            return AVERROR(ENOMEM);
        }
    }
    (*videomaster_context)->avctx = avctx;
    (*videomaster_data)->context = *videomaster_context;
    av_log(avctx, AV_LOG_TRACE,
           "ff_videomaster_extract_context: "
           "OUT\n");
    return 0;
}

int ff_videomaster_get_api_info(VideoMasterContext *videomaster_context)
{
    int av_error = AVERROR(EIO);
    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_get_api_info: IN\n");
    if (ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_GetApiInfo(&videomaster_context->api_version,
                           &videomaster_context->number_of_boards),
            "API version retrieved "
            "successfully",
            "Failed to retrieve API "
            "version") == 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_INFO, "API Version: %u\n",
               videomaster_context->api_version);
        av_log(videomaster_context->avctx, AV_LOG_INFO,
               "Number of Boards: %u\n", videomaster_context->number_of_boards);

        av_error = 0;
    }
    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_get_api_info: OUT\n");
    return av_error;
}

int ff_videomaster_get_audio_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterBufferPacking buffer_packing,
    enum AVVideoMasterChannelType *channel_type,
    union VideoMasterAudioInfo *audio_info, uint32_t *sample_rate,
    uint32_t *nb_channels, uint32_t *sample_size, enum AVCodecID *codec)
{
    int    av_error = 0;
    HANDLE local_stream_handle = stream_handle;

    av_log(avctx, AV_LOG_TRACE,
           "ff_videomaster_get_audio_stream_"
           "properties: IN\n");

    *channel_type = ff_videomaster_get_channel_type_from_index(avctx,
                                                               board_handle,
                                                               channel_index);

    if (*channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {
        if (local_stream_handle == NULL)
            ff_videomaster_handle_vhd_status(
                avctx,
                VHD_OpenStreamHandle(
                    board_handle,
                    ff_videomaster_get_rx_stream_type_from_index(channel_index),
                    ff_videomaster_get_stream_proc_hdmi(), NULL,
                    &local_stream_handle, NULL),
                "Stream handle opened "
                "successfully",
                "Failed to open stream "
                "handle");

        GET_AND_CHECK(
            ff_videomaster_handle_av_error, avctx, avctx,
            ff_videomaster_get_audio_stream_properties_from_audio_infoframe_hdmi(
                avctx, board_handle, local_stream_handle, channel_index,
                buffer_packing, audio_info, sample_rate, nb_channels,
                sample_size, codec),
            "Get audio properties",
            "Failed to get audio stream "
            "properties");

        if (stream_handle == NULL)
            ff_videomaster_handle_vhd_status(avctx,
                                             VHD_CloseStreamHandle(
                                                 local_stream_handle),
                                             "Stream handle closed "
                                             "successfully",
                                             "Failed to close stream "
                                             "handle");
    }
    else
    {

        av_log(avctx, AV_LOG_WARNING,
               "Cannot retrieve audio stream "
               "properties for SDI stream\n");
    }

    av_log(avctx, AV_LOG_TRACE,
           "ff_videomaster_get_audio_stream_"
           "properties: OUT\n");
    return 0;
}

enum AVVideoMasterChannelType ff_videomaster_get_channel_type_from_index(
    AVFormatContext *avctx, HANDLE board_handle, int channel_index)
{
    VHD_CHANNELTYPE channel_type = NB_VHD_CHANNELTYPE;

    ff_videomaster_handle_vhd_status(
        avctx,
        VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                               VHD_CORE_CP_TYPE, (uint32_t *)&channel_type),
        "", "");

    switch (channel_type)
    {
    case VHD_CHNTYPE_HDMI_TMDS:
    case VHD_CHNTYPE_HDMI_FRL3:
    case VHD_CHNTYPE_HDMI_FRL4:
    case VHD_CHNTYPE_HDMI_FRL5:
    case VHD_CHNTYPE_HDMI_FRL6:
        return AV_VIDEOMASTER_CHANNEL_HDMI;
    case VHD_CHNTYPE_HDSDI:
    case VHD_CHNTYPE_3GSDI:
    case VHD_CHNTYPE_12GSDI:
        return AV_VIDEOMASTER_CHANNEL_SDI;
    case VHD_CHNTYPE_3GSDI_ASI:
    case VHD_CHNTYPE_12GSDI_ASI:
        return AV_VIDEOMASTER_CHANNEL_ASISDI;
    case VHD_CHNTYPE_IP_2110:
        return AV_VIDEOMASTER_CHANNEL_IP_2110;
    default:
        break;
    }

    return AV_VIDEOMASTER_CHANNEL_UNKNOWN;
}

int ff_videomaster_reject_ip_params(VideoMasterData    *data,
                                    VideoMasterContext *ctx)
{
    const char *tech = ff_videomaster_channel_type_to_string(ctx->channel_type);

    if (data->ip_video_destination != NULL ||
        data->ip_video_sps_destination != NULL ||
        data->ip_video_source != NULL || data->ip_video_sps_source != NULL ||
        data->ip_video_sdp_file != NULL || data->ip_video_udp_port > 0 ||
        data->ip_video_sps_udp_port > 0 || data->ip_video_udp_port_src > 0 ||
        data->ip_video_sps_udp_port_src > 0 || data->ip_video_width > 0 ||
        data->ip_video_height > 0 || data->ip_video_framerate_num > 0 ||
        data->ip_video_framerate_den > 0 || data->ip_video_interlaced >= 0 ||
        data->ip_video_bit_depth > 0)
    {
        av_log(ctx->avctx, AV_LOG_ERROR,
               "ip_video_* arguments are not applicable for %s channels.\n",
               tech);
        return AVERROR(EINVAL);
    }

    return 0;
}

int ff_videomaster_get_data(VideoMasterContext *videomaster_context)
{
    int lock_slot_status = lock_slot(videomaster_context);

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_get_data: IN\n");

    if (lock_slot_status == AVERROR(EAGAIN))
    {
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Timeout while waiting for "
               "slot lock\n");
        return AVERROR(EAGAIN);
    }
    else if (lock_slot_status != 0)
        return AVERROR(EIO);

    if (videomaster_context->has_video &&
        get_video_buffer(videomaster_context) != 0)
        return AVERROR(EIO);

    if (videomaster_context->has_audio &&
        get_audio_buffer(videomaster_context) != 0)
        return AVERROR(EIO);

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_get_data: OUT\n");

    return 0;
}

int ff_videomaster_get_nb_rx_channels(VideoMasterContext *videomaster_context)
{
    return ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_GetBoardProperty(videomaster_context->board_handle,
                             VHD_CORE_BP_NB_RXCHANNELS,
                             &videomaster_context->nb_rx_channels),
        "Number of RX channels retrieved "
        "successfully",
        "Failed to retrieve number of RX "
        "channels");
}

int ff_videomaster_get_slots_counter(VideoMasterContext *videomaster_context)
{
    VHD_GetStreamProperty(videomaster_context->stream_handle,
                          VHD_CORE_SP_SLOTS_COUNT,
                          &videomaster_context->frames_received);
    VHD_GetStreamProperty(videomaster_context->stream_handle,
                          VHD_CORE_SP_SLOTS_DROPPED,
                          &videomaster_context->frames_dropped);
    return 0;
}

int ff_videomaster_get_audio_slots_counter(
    VideoMasterContext *videomaster_context)
{
    VHD_GetStreamProperty(videomaster_context->ip_audio_stream_handle,
                          VHD_CORE_SP_SLOTS_COUNT,
                          &videomaster_context->audio_slots_received);
    VHD_GetStreamProperty(videomaster_context->ip_audio_stream_handle,
                          VHD_CORE_SP_SLOTS_DROPPED,
                          &videomaster_context->audio_slots_dropped);
    return 0;
}

int ff_videomaster_get_nb_tx_channels(VideoMasterContext *videomaster_context)
{
    return ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_GetBoardProperty(videomaster_context->board_handle,
                             VHD_CORE_BP_NB_TXCHANNELS,
                             &videomaster_context->nb_tx_channels),
        "Number of TX channels retrieved "
        "successfully",
        "Failed to retrieve number of TX "
        "channels");
}

int ff_videomaster_get_timestamp(VideoMasterContext *videomaster_context,
                                 void               *slot_handle,
                                 enum AVVideoMasterTimeStampType source,
                                 uint64_t                       *timestamp)
{
    int          av_error = 0;
    uint32_t     clock_frequency = 0;
    VHD_TIMECODE time_code;
    float        total_frames = 0;
    if (slot_handle == NULL)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Slot handle is NULL, cannot "
               "get timestamp\n");
        return AVERROR(EINVAL);
    }

    if (source == AV_VIDEOMASTER_TIMESTAMP_HARDWARE)
    {
        bool      is_audio_slot = videomaster_context->ip_audio_slot_handle !=
                                      NULL &&
                                  slot_handle ==
                                      videomaster_context->ip_audio_slot_handle;
        uint64_t *hw_ts_base = is_audio_slot
                                   ? &videomaster_context->hw_ts_base_audio
                                   : &videomaster_context->hw_ts_base_video;

        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_GetSlotHardwareTimestamp(slot_handle, timestamp,
                                                   &clock_frequency),
                      "Hardware Timestamp retrieved "
                      "successfully",
                      "Failed to retrieve hardware "
                      "timestamp");
        *timestamp = (*timestamp * 1000000) / clock_frequency;
        /* Normalize to start near 0 per essence, same as system/osc below. */
        if (*hw_ts_base == 0)
            *hw_ts_base = *timestamp;
        *timestamp -= *hw_ts_base;
        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "Hardware timestamp (%s): %lli\n",
               is_audio_slot ? "audio" : "video", *timestamp);
    }
    else if (source == AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD ||
             source == AV_VIDEOMASTER_TIMESTAMP_LTC_COMPANION_CARD)
    {
        GET_AND_CHECK(
            ff_videomaster_handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_GetSlotTimecode(
                slot_handle,
                (VHD_TIMECODE_SOURCE)
                    get_videomaster_enumeration_value_for_timestamp_source(
                        source),
                &time_code),
            "LTC Timestamp retrieved "
            "successfully",
            "Failed to retrieve LTC "
            "timestamp");
        total_frames = ((time_code.Hour * 3600) + (time_code.Minute * 60) +
                        time_code.Second) *
                           videomaster_context->ltc_frame_rate +
                       time_code.Frame;
        *timestamp = (uint64_t)((total_frames * 1000000.0) /
                                videomaster_context->ltc_frame_rate);

        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "Timecode: %02d:%02d:%02d:%02d - Computed timestamp: %lli\n",
               time_code.Hour, time_code.Minute, time_code.Second,
               time_code.Frame, *timestamp);
    }
    else if (source == AV_VIDEOMASTER_TIMESTAMP_RTP)
    {
        ULONG raw_rtp_timestamp = 0;
        bool  is_audio_slot = videomaster_context->ip_audio_slot_handle !=
                                  NULL &&
                              slot_handle ==
                                  videomaster_context->ip_audio_slot_handle;
        bool *initialized =
            is_audio_slot ? &videomaster_context->rtp_ts_initialized_audio
                          : &videomaster_context->rtp_ts_initialized_video;
        uint32_t *last_raw = is_audio_slot
                                 ? &videomaster_context->rtp_ts_last_raw_audio
                                 : &videomaster_context->rtp_ts_last_raw_video;
        int64_t *unwrapped = is_audio_slot
                                 ? &videomaster_context->rtp_ts_unwrapped_audio
                                 : &videomaster_context->rtp_ts_unwrapped_video;
        /* Video RTP clock is fixed at 90 kHz (RFC 4175 / ST2110-20). */
        uint32_t clock_rate = is_audio_slot
                                  ? videomaster_context->audio_sample_rate
                                  : 90000;
        int64_t  pts_us;

        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_GetSlotRTPTimeStamp(slot_handle, &raw_rtp_timestamp),
                      "RTP timestamp retrieved successfully",
                      "Failed to retrieve RTP timestamp");

        /* Unwrap into a monotonic 64-bit count: a signed 32-bit delta
         * survives wraparound as long as the real gap stays under 2^31. */
        if (!*initialized)
        {
            *unwrapped = 0;
            *initialized = true;
        }
        else
        {
            *unwrapped += (int32_t)(raw_rtp_timestamp - *last_raw);
        }
        *last_raw = raw_rtp_timestamp;

        pts_us = av_rescale(*unwrapped, 1000000, clock_rate);
        if (pts_us < 0)
            pts_us = 0;
        *timestamp = (uint64_t)pts_us;

        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "RTP timestamp (%s): raw=%u unwrapped=%lli -> %llu us\n",
               is_audio_slot ? "audio" : "video", raw_rtp_timestamp,
               (long long)*unwrapped, (unsigned long long)*timestamp);
    }
    else if (source == AV_VIDEOMASTER_TIMESTAMP_PTP)
    {
        static uint64_t ptp_ts_base = 0;
        ULONG           ptp_sec = 0, ptp_nsec = 0;

        /* Board-level absolute time, not tied to a slot: this SDK has no
         * per-slot PTP timestamp API. */
        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_GetPTPTime(videomaster_context->board_handle,
                                     &ptp_sec, &ptp_nsec),
                      "PTP time retrieved successfully",
                      "Failed to retrieve PTP time");
        *timestamp = (uint64_t)ptp_sec * 1000000 + ptp_nsec / 1000;
        if (ptp_ts_base == 0)
            ptp_ts_base = *timestamp;
        *timestamp -= ptp_ts_base;

        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "PTP timestamp: %llu us\n", (unsigned long long)*timestamp);
    }
    else
    {
        bool      is_audio_slot = videomaster_context->ip_audio_slot_handle !=
                                      NULL &&
                                  slot_handle ==
                                      videomaster_context->ip_audio_slot_handle;
        uint64_t *system_ts_base =
            is_audio_slot ? &videomaster_context->system_ts_base_audio
                          : &videomaster_context->system_ts_base_video;

        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_GetSlotSystemTime(slot_handle, timestamp),
                      "Timestamp retrieved "
                      "successfully",
                      "Failed to retrieve timestamp");
        /* Raw SDK timestamp next to a host clock, for pts/drift diagnostics. */
        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "Raw system timestamp (%s): %llu us (host ref: %lld us)\n",
               is_audio_slot ? "audio" : "video",
               (unsigned long long)*timestamp,
               (long long)av_gettime_relative());
        /* Normalize to start near 0 per essence: the clock type (osc/system)
         * is board-wide, but in IP non-sync mode video and audio are two
         * independently started SDK streams whose per-slot system time
         * counters don't share a common epoch (see system_ts_base_* doc). */
        if (*system_ts_base == 0)
            *system_ts_base = *timestamp;
        *timestamp -= *system_ts_base;
        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "System timestamp (%s): %lli\n",
               is_audio_slot ? "audio" : "video", *timestamp);
    }

    return 0;
}

int ff_videomaster_get_video_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterChannelType *channel_type,
    union VideoMasterVideoInfo *video_info, uint32_t *width, uint32_t *height,
    uint32_t *frame_rate_num, uint32_t *frame_rate_den, bool *interlaced,
    bool dual_stream)
{
    int av_status = 0;

    *channel_type = ff_videomaster_get_channel_type_from_index(avctx,
                                                               board_handle,
                                                               channel_index);
    av_log(avctx, AV_LOG_TRACE,
           "ff_videomaster_get_video_stream_"
           "properties: IN\n");

    if (*channel_type == AV_VIDEOMASTER_CHANNEL_IP_2110)
    {
        /*
         * IP ST2110 channels do not support passive video-standard
         * auto-detection. Properties (resolution, frame-rate, …) require
         * explicit user parameters (ip_video_* options) and a running
         * stream.  Return ENOSYS so callers can display a fallback
         * description without printing a misleading SDI error.
         */
        av_log(avctx, AV_LOG_DEBUG,
               "IP 2110 channel %u: video properties require explicit mode "
               "(ip_video_* options). Auto-detection is not available.\n",
               channel_index);
        return AVERROR(ENOSYS);
    }

    if (*channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {
        av_status = ff_videomaster_get_video_stream_properties_hdmi(
            avctx, board_handle, stream_handle, channel_index, video_info,
            width, height, frame_rate_num, frame_rate_den, interlaced);
        if (av_status != 0)
            return av_status;
    }
    else
    {
        av_status = ff_videomaster_get_video_stream_properties_sdi(
            avctx, board_handle, stream_handle, channel_index, video_info,
            width, height, frame_rate_num, frame_rate_den, interlaced,
            dual_stream);
        if (av_status != 0)
            return av_status;
    }

    av_log(avctx, AV_LOG_TRACE,
           "ff_videomaster_get_video_stream_"
           "properties: OUT\n");
    return 0;
}

bool ff_videomaster_is_3g_b_ds_interface_supported(
    VideoMasterContext *videomaster_context)
{
    BOOL32 interface_supported = false;
    if (videomaster_context->board_handle)
    {
        enum AVVideoMasterChannelType channel_type =
            ff_videomaster_get_channel_type_from_index(
                videomaster_context->avctx, videomaster_context->board_handle,
                videomaster_context->channel_index);
        int stream_type = ff_videomaster_get_rx_stream_type_from_index(
            videomaster_context->channel_index);

        if (stream_type == NB_VHD_STREAMTYPES)
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Unsupported channel index %u for SDI "
                   "stream type\n",
                   videomaster_context->channel_index);
            return false;
        }

        if (channel_type == AV_VIDEOMASTER_CHANNEL_SDI)
            VHD_GetBoardCapSDIInterface(videomaster_context->board_handle,
                                        stream_type,
                                        VHD_INTERFACE_3G_B_DS_425_1,
                                        &interface_supported);
        else
        {
            av_log(videomaster_context->avctx, AV_LOG_DEBUG,
                   "Channel type is not SDI\n");
        }
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }
    return !!interface_supported;
}

bool ff_videomaster_is_channel_locked(VideoMasterContext *videomaster_context)
{
    enum AVVideoMasterChannelType channel_type =
        ff_videomaster_get_channel_type_from_index(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->channel_index);

    if (channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
        return ff_videomaster_is_channel_locked_hdmi(videomaster_context);
    else if (channel_type == AV_VIDEOMASTER_CHANNEL_IP_2110)
        return ff_videomaster_is_channel_locked_ip(videomaster_context);
    else if (channel_type == AV_VIDEOMASTER_CHANNEL_SDI ||
             channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI)
        return ff_videomaster_is_channel_locked_sdi(videomaster_context);

    av_log(videomaster_context->avctx, AV_LOG_DEBUG,
           "Unknown channel type for channel %u, considered unlocked\n",
           videomaster_context->channel_index);

    /* Last resort: check the board type.  On IP boards the channel type
     * property (VHD_CORE_CP_TYPE) may not return VHD_CHNTYPE_IP_2110 when
     * no stream handle is open, which causes UNKNOWN to be returned above.
     * Treat all channels of an IP board as available.
     */
    {
        ULONG board_type = 0;
        VHD_GetBoardProperty(videomaster_context->board_handle,
                             VHD_CORE_BP_BOARD_TYPE, &board_type);
        if (board_type == VHD_BOARDTYPE_IP)
        {
            av_log(videomaster_context->avctx, AV_LOG_DEBUG,
                   "Board %u is an IP board: treating channel %u as "
                   "available regardless of detected channel type\n",
                   videomaster_context->board_index,
                   videomaster_context->channel_index);
            return ff_videomaster_is_channel_locked_ip(videomaster_context);
        }
    }
    return false;
}

bool ff_videomaster_is_hardware_timestamp_supported(
    VideoMasterContext *videomaster_context)
{
    ULONG hardware_timestamp_supported = 0;
    if (videomaster_context->board_handle)
        VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_TIMESTAMP,
                               &hardware_timestamp_supported);
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }
    return !!hardware_timestamp_supported;
}

bool ff_videomaster_is_ltc_companion_card_present(
    VideoMasterContext *videomaster_context)
{
    BOOL32 ltc_companion_card_present = false;
    if (videomaster_context->board_handle)
    {
        if (ff_videomaster_is_ltc_companion_card_supported(videomaster_context))
        {
            VHD_DetectCompanionCard(videomaster_context->board_handle,
                                    VHD_LTC_COMPANION_CARD,
                                    &ltc_companion_card_present);
        }
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }

    return !!ltc_companion_card_present;
}

bool ff_videomaster_is_ltc_companion_card_supported(
    VideoMasterContext *videomaster_context)
{
    ULONG ltc_companion_card_feature_supported = 0;
    if (videomaster_context->board_handle)
        VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_LTC_COMPANION_CARD,
                               &ltc_companion_card_feature_supported);
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }
    return !!ltc_companion_card_feature_supported;
}

bool ff_videomaster_is_ltc_on_board_timestamp_supported(
    VideoMasterContext *videomaster_context)
{
    ULONG ltc_on_board_timestamp_supported = 0;
    if (videomaster_context->board_handle)
        VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_LTC_ONBOARD,
                               &ltc_on_board_timestamp_supported);
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }

    return !!ltc_on_board_timestamp_supported;
}

bool ff_videomaster_is_ptp_supported(VideoMasterContext *videomaster_context)
{
    ULONG ptp_supported = 0;
    if (videomaster_context->board_handle)
        VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_IP_BOARD_CAP_PTP, &ptp_supported);
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }

    return !!ptp_supported;
}

bool ff_videomaster_is_ptp_locked(VideoMasterContext *videomaster_context)
{
    VHD_PTP_PORT_STATE ptp_port_state;
    BOOL32             ptp_locked = FALSE;

    if (!videomaster_context->board_handle)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
        return false;
    }
    if (VHD_GetPTPPortState(videomaster_context->board_handle, &ptp_port_state,
                            &ptp_locked) != VHDERR_NOERROR)
        return false;

    return !!ptp_locked;
}

int ff_videomaster_open_board_handle(VideoMasterContext *videomaster_context)
{
    return ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_OpenBoardHandle(videomaster_context->board_index,
                            &videomaster_context->board_handle, NULL, 0),
        "Board handle opened successfully", "Failed to open board handle");
}

int ff_videomaster_open_stream_handle(VideoMasterContext *videomaster_context)
{
    uint32_t stream_proc = ff_videomaster_get_stream_proc_hdmi();

    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_SDI ||
        videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI)
        stream_proc = ff_videomaster_get_stream_proc_sdi();
    else if (videomaster_context->channel_type != AV_VIDEOMASTER_CHANNEL_HDMI)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Unsupported channel type %d\n",
               videomaster_context->channel_type);
        return AVERROR(EINVAL);
    }
    // Open stream as JOINED to get audio and
    // video data
    return ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_OpenStreamHandle(videomaster_context->board_handle,
                             ff_videomaster_get_rx_stream_type_from_index(
                                 videomaster_context->channel_index),
                             stream_proc, NULL,
                             &videomaster_context->stream_handle, NULL),
        "Stream handle opened successfully", "Failed to open stream handle");
}

int ff_videomaster_release_data(VideoMasterContext *videomaster_context)
{

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_release_data: IN\n");
    if (videomaster_context->audio_buffer)
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Freeing audio buffer of size "
               "%u\n",
               videomaster_context->audio_buffer_size);
        av_freep(&videomaster_context->audio_buffer);
        videomaster_context->audio_buffer = NULL;
    }
    videomaster_context->audio_buffer_size = 0;

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_release_data: OUT\n");

    return unlock_slot(videomaster_context);
}

const char *ff_videomaster_sample_rate_to_string(
    enum AVVideoMasterSampleRateValue sample_rate)
{
    switch (sample_rate)
    {
    case AV_VIDEOMASTER_SAMPLE_RATE_32000:
        return "32 kHz";
    case AV_VIDEOMASTER_SAMPLE_RATE_44100:
        return "44.1 kHz";
    case AV_VIDEOMASTER_SAMPLE_RATE_48000:
        return "48 kHz";
    default:
        return "Unknown sample rate";
    }
}

const char *ff_videomaster_sample_size_to_string(
    enum AVVideoMasterSampleSizeValue sample_size)
{
    switch (sample_size)
    {
    case AV_VIDEOMASTER_SAMPLE_SIZE_16:
        return "16 bits";
    case AV_VIDEOMASTER_SAMPLE_SIZE_24:
        return "24 bits";
    default:
        return "Unknown sample size";
    }
}

int ff_videomaster_start_stream(VideoMasterContext *videomaster_context)
{
    int av_error = 0;

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_start_stream: IN\n");

    /* 1. Tech-specific dispatcher: configure stream properties for the channel
     * type */
    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
        av_error = ff_videomaster_start_stream_hdmi(videomaster_context);
    else if (videomaster_context->channel_type ==
             AV_VIDEOMASTER_CHANNEL_IP_2110)
        av_error = ff_videomaster_start_stream_ip(videomaster_context);
    else if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_SDI ||
             videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI)
        av_error = ff_videomaster_start_stream_sdi(videomaster_context);

    if (av_error != 0)
        return av_error;

    /* 2. Common setup: applies to all technologies */
    GET_AND_CHECK(setup_field_merge, videomaster_context->avctx,
                  videomaster_context);
    GET_AND_CHECK(setup_transfer_scheme, videomaster_context->avctx,
                  videomaster_context);

    /* IP buffer packing, codec and pixel format are already configured in
     * ff_videomaster_start_stream_ip_explicit; skip generic detection. */
    if (videomaster_context->channel_type != AV_VIDEOMASTER_CHANNEL_IP_2110)
    {
        av_error = setup_buffer_packing(videomaster_context);
        if (av_error != 0)
            return av_error;
    }

    /* 3. Configure I/O timeout. */
    if (videomaster_context->channel_type != AV_VIDEOMASTER_CHANNEL_IP_2110)
    {
        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_SetStreamProperty(videomaster_context->stream_handle,
                                            VHD_CORE_SP_IO_TIMEOUT,
                                            VIDEOMASTER_LOCK_SLOT_TIMEOUT_MS),
                      "Stream time-out has been set to " AV_STRINGIFY(
                          VIDEOMASTER_LOCK_SLOT_TIMEOUT_MS) "ms",
                      "Unable to set stream time-out");
    }
    else
    {
        if (videomaster_context->ip_sync_mode)
        {
            GET_AND_CHECK(
                ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                videomaster_context->avctx,
                VHD_SetStreamProperty(videomaster_context->ip_sync_handle,
                                      VHD_CORE_SP_IO_TIMEOUT,
                                      VIDEOMASTER_IP_LOCK_SLOT_TIMEOUT_MS),
                "Sync stream time-out has been set to " AV_STRINGIFY(
                    VIDEOMASTER_IP_LOCK_SLOT_TIMEOUT_MS) "ms",
                "Unable to set sync stream time-out");
        }
        else
        {
            if (videomaster_context->has_video)
            {
                GET_AND_CHECK(
                    ff_videomaster_handle_vhd_status,
                    videomaster_context->avctx, videomaster_context->avctx,
                    VHD_SetStreamProperty(videomaster_context->stream_handle,
                                          VHD_CORE_SP_IO_TIMEOUT,
                                          VIDEOMASTER_IP_LOCK_SLOT_TIMEOUT_MS),
                    "Video stream time-out has been set to " AV_STRINGIFY(
                        VIDEOMASTER_IP_LOCK_SLOT_TIMEOUT_MS) "ms",
                    "Unable to set video stream time-out");
            }
            if (videomaster_context->has_audio)
            {
                GET_AND_CHECK(
                    ff_videomaster_handle_vhd_status,
                    videomaster_context->avctx, videomaster_context->avctx,
                    VHD_SetStreamProperty(
                        videomaster_context->ip_audio_stream_handle,
                        VHD_CORE_SP_IO_TIMEOUT,
                        VIDEOMASTER_IP_LOCK_SLOT_TIMEOUT_MS),
                    "Audio stream time-out has been set to " AV_STRINGIFY(
                        VIDEOMASTER_IP_LOCK_SLOT_TIMEOUT_MS) "ms",
                    "Unable to set audio stream time-out");
            }
        }
    }

    /* 4. Configure timestamp source */
    av_error = setup_timestamp_source(videomaster_context);
    if (av_error != 0)
        return av_error;

    /* 5. Start the stream(s) */
    {
        bool   is_ip = videomaster_context->channel_type ==
                       AV_VIDEOMASTER_CHANNEL_IP_2110;
        HANDLE start_handle = videomaster_context->stream_handle;
        if (is_ip)
        {
            if (videomaster_context->ip_sync_mode)
                start_handle = videomaster_context->ip_sync_handle;
            else if (!videomaster_context->has_video &&
                     videomaster_context->has_audio)
                start_handle = videomaster_context->ip_audio_stream_handle;
        }
        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_StartStream(start_handle),
                      "Stream started successfully", "Failed to start stream");

        /* Non-sync mode with both essences: video and audio are two
         * independent streams, so audio needs its own VHD_StartStream and
         * its own capture thread — a thread that pulls audio slots at its
         * own pace, decoupled from however often read_packet() is called
         * for video (see ff_videomaster_start_ip_audio_thread()). */
        if (is_ip && !videomaster_context->ip_sync_mode &&
            videomaster_context->has_video && videomaster_context->has_audio)
        {
            GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                          videomaster_context->avctx,
                          videomaster_context->avctx,
                          VHD_StartStream(
                              videomaster_context->ip_audio_stream_handle),
                          "Audio stream started successfully",
                          "Failed to start audio stream");

            av_error = ff_videomaster_start_ip_audio_thread(
                videomaster_context);
            if (av_error != 0)
                return av_error;
        }
    }

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_start_stream: OUT\n");

    return 0;
}

int ff_videomaster_stop_stream(VideoMasterContext *videomaster_context)
{
    release_audio_info(videomaster_context,
                       &videomaster_context->audio_info.sdi.audio_info);

    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_IP_2110)
    {
        HANDLE stop_handle = videomaster_context->stream_handle;
        if (videomaster_context->ip_sync_mode)
            stop_handle = videomaster_context->ip_sync_handle;
        else if (!videomaster_context->has_video &&
                 videomaster_context->has_audio)
            stop_handle = videomaster_context->ip_audio_stream_handle;

        int ret = ff_videomaster_handle_vhd_status(
            videomaster_context->avctx, VHD_StopStream(stop_handle),
            "Stream stopped successfully", "Failed to stop stream");

        /* Non-sync mode with both essences: video and audio are two
         * independent streams that were both started in
         * ff_videomaster_start_stream(), so both must be stopped too. */
        if (!videomaster_context->ip_sync_mode &&
            videomaster_context->has_video && videomaster_context->has_audio)
        {
            /* Before stopping the audio stream: the thread may still be
             * reading a locked slot's buffer. */
            ff_videomaster_stop_ip_audio_thread(videomaster_context);

            int audio_ret = ff_videomaster_handle_vhd_status(
                videomaster_context->avctx,
                VHD_StopStream(videomaster_context->ip_audio_stream_handle),
                "Audio stream stopped successfully",
                "Failed to stop audio stream");
            if (ret == 0)
                ret = audio_ret;
        }

        ff_videomaster_leave_multicast_group(videomaster_context);
        ff_videomaster_close_streams_ip(videomaster_context);
        return ret;
    }

    return ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_StopStream(videomaster_context->stream_handle),
        "Stream stopped successfully", "Failed to stop stream");
}

const char *ff_videomaster_timestamp_type_to_string(
    enum AVVideoMasterTimeStampType timestamp_type)
{
    switch (timestamp_type)
    {
    case AV_VIDEOMASTER_TIMESTAMP_OSCILLATOR:
        return "osc";
    case AV_VIDEOMASTER_TIMESTAMP_SYSTEM:
        return "system";
    case AV_VIDEOMASTER_TIMESTAMP_HARDWARE:
        return "hw";
    case AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD:
        return "ltc_onboard";
    case AV_VIDEOMASTER_TIMESTAMP_LTC_COMPANION_CARD:
        return "ltc_companion";
    case AV_VIDEOMASTER_TIMESTAMP_RTP:
        return "rtp";
    case AV_VIDEOMASTER_TIMESTAMP_PTP:
        return "ptp";
    default:
        return "unknown";
    }
}

void ff_videomaster_packet_queue_init(AVFormatContext        *avctx,
                                      VideoMasterPacketQueue *q,
                                      int64_t                 max_q_size)
{
    memset(q, 0, sizeof(*q));
    ff_mutex_init(&q->mutex, NULL);
    ff_cond_init(&q->cond, NULL);
    q->avctx = avctx;
    q->max_q_size = max_q_size;
}

static void videomaster_packet_queue_flush(VideoMasterPacketQueue *q)
{
    AVPacket pkt;

    ff_mutex_lock(&q->mutex);
    while (avpriv_packet_list_get(&q->pkt_list, &pkt) == 0)
        av_packet_unref(&pkt);
    q->nb_packets = 0;
    q->size = 0;
    ff_mutex_unlock(&q->mutex);
}

void ff_videomaster_packet_queue_end(VideoMasterPacketQueue *q)
{
    videomaster_packet_queue_flush(q);
    ff_mutex_destroy(&q->mutex);
    ff_cond_destroy(&q->cond);
}

void ff_videomaster_packet_queue_abort(VideoMasterPacketQueue *q)
{
    ff_mutex_lock(&q->mutex);
    q->abort_request = 1;
    ff_cond_broadcast(&q->cond);
    ff_mutex_unlock(&q->mutex);
}

int ff_videomaster_packet_queue_put(VideoMasterPacketQueue *q, AVPacket *pkt)
{
    int pkt_size = pkt->size;
    int ret;

    if ((uint64_t)q->size > (uint64_t)q->max_q_size)
    {
        av_packet_unref(pkt);
        av_log(q->avctx, AV_LOG_WARNING,
               "VideoMaster IP audio queue overrun (non-sync combined "
               "mode): dropping packet\n");
        return -1;
    }
    if (av_packet_make_refcounted(pkt) < 0)
    {
        av_packet_unref(pkt);
        return -1;
    }

    ff_mutex_lock(&q->mutex);
    ret = avpriv_packet_list_put(&q->pkt_list, pkt, NULL, 0);
    if (ret == 0)
    {
        q->nb_packets++;
        q->size += pkt_size + sizeof(AVPacket);
        ff_cond_signal(&q->cond);
    }
    else
    {
        av_packet_unref(pkt);
    }
    ff_mutex_unlock(&q->mutex);
    return ret;
}

int ff_videomaster_packet_queue_get(VideoMasterPacketQueue *q, AVPacket *pkt,
                                    int block)
{
    int ret;

    ff_mutex_lock(&q->mutex);
    for (;;)
    {
        if (q->abort_request)
        {
            ret = AVERROR(EAGAIN);
            break;
        }
        ret = avpriv_packet_list_get(&q->pkt_list, pkt);
        if (ret == 0)
        {
            q->nb_packets--;
            q->size -= pkt->size + sizeof(AVPacket);
            break;
        }
        else if (!block)
        {
            ret = AVERROR(EAGAIN);
            break;
        }
        else
        {
            ff_cond_wait(&q->cond, &q->mutex);
        }
    }
    ff_mutex_unlock(&q->mutex);
    return ret;
}
