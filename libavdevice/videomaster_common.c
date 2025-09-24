#include "videomaster_common.h"
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include <stdio.h>

#if defined(__APPLE__)
#include <VideoMasterHD/VideoMasterHD_Core.h>
#include <VideoMasterHD/VideoMasterHD_String.h>
#else
#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_String.h>
#endif

#define GET_AND_CHECK(func, avctx, ...)                                        \
    do                                                                         \
    {                                                                          \
        av_error = func(__VA_ARGS__);                                          \
        if (av_error != 0)                                                     \
        {                                                                      \
            av_log(avctx, AV_LOG_TRACE, "Early ending of function.\n");        \
            return av_error;                                                   \
        }                                                                      \
    } while (0)

#define GET_AND_CHECK_AND_STOP_STREAM(func, avctx, ...)                        \
    do                                                                         \
    {                                                                          \
        av_error = func(__VA_ARGS__);                                          \
        if (av_error != 0)                                                     \
        {                                                                      \
            av_log(avctx, AV_LOG_TRACE, "Early ending of function.\n");        \
            ff_videomaster_stop_stream(videomaster_context);                   \
            return av_error;                                                   \
        }                                                                      \
    } while (0)

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
 * @brief Disable loopback on the channel
 *
 * This function disables the loopback on the channel
 * specified in the VideoMasterContext.
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @return int 0 on success, negative AVERROR code on failure
 */
static int disable_loopback_on_channel(VideoMasterContext *videomaster_context);

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
 * @brief Get the audio stream properties such as channel number, codec id,
 * sample size or sample frequency from audio infoframe and aes status for HDMI
 * stream
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param audio_info Pointer to the variable to store the audio information
 * @param codec  Pointer to the variable to store the codec ID
 * @return int 0 on success, negative AVERROR code on failure
 */
static int get_audio_stream_properties_from_audio_infoframe(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, union VideoMasterAudioInfo *audio_info,
    uint32_t *sample_rate, uint32_t *nb_channels, uint32_t *sample_size,
    enum AVCodecID *codec);

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
 * @brief Get the codec id from audio infoframe and aes status object
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param audio_info_frame  Audio infoframe
 * @param aes_status  AES status object
 * @param sample_size  Pointer to the variable to store the sample size
 * @return int 0 on success, negative AVERROR code on failure
 */
static int get_codec_from_audio_infoframe_and_aes_status(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    enum AVCodecID *codec);

/**
 * @brief Get the firmware loopback property for a given channel index
 *
 * @param channel_index  Index of the channel
 * @return VHD_CORE_BOARDPROPERTY  The firmware loopback property for the
 * channel
 */
static VHD_CORE_BOARDPROPERTY get_firmware_loopback_property(int channel_index);

/**
 * @brief Get the nb channels from audio infoframe and aes status object
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param audio_info_frame  Audio infoframe
 * @param aes_status  AES status object
 * @param nb_channels  Pointer to the variable to store the number of channels
 * @return int 0 on success, negative AVERROR code on failure
 */
static int get_nb_channels_from_audio_infoframe_and_aes_status(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    uint32_t *nb_channels);

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
static VHD_STREAMTYPE get_rx_stream_type_from_index(uint32_t index);

/**
 * @brief Get the sample rate from audio infoframe and aes status object
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param audio_info_frame  Audio infoframe
 * @param aes_status  AES status object
 * @param sample_rate  Pointer to the variable to store the sample rate
 * @return int 0 on success, negative AVERROR code on failure
 */
static int get_sample_rate_from_audio_infoframe_and_aes_status(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    uint32_t *sample_rate);

/**
 * @brief Get the sample size from audio infoframe and aes status object
 *
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param audio_info_frame  Audio infoframe
 * @param aes_status  AES status object
 * @param sample_size  Pointer to the variable to store the sample size
 * @return int 0 on success, negative AVERROR code on failure
 */
static int get_sample_size_from_audio_infoframe_and_aes_status(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    uint32_t *sample_size);

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
 * @brief      Handles AV error codes and logs messages accordingly.
 *
 * This function checks the AV error code and logs a success message if
 * the error code is 0. If the error code is any other error, it logs
 * the error message.
 *
 * @param avctx  AVFormatContext pointer to the AVFormatContext
 * @param av_error  AVERROR code
 * @param trace_message  Message to log for tracing
 * @param error_message  Message to log on error
 * @return int 0 on success, or negative AVERROR code on failure
 */
static int handle_av_error(AVFormatContext *avctx, int av_error,
                           const char *trace_message,
                           const char *error_message);

/**
 * @brief  Handles VHD status codes and logs messages accordingly.
 *
 *  This function checks the VHD status code and logs a success message if
 *  the status is VHDERR_NOERROR. If the status is VHDERR_TIMEOUT, it
 * returns AVERROR(EAGAIN). If the status is any other error, it logs the
 * error message and the last error message from the VHD library.
 *
 * @param avctx  AVFormatContext pointer to the AVFormatContext
 * @param vhd_status VHD_ERRORCODE status code
 * @param success_message Message to log on success
 * @param error_message Message to log on error
 * @return int 0 on success, or negative AVERROR code on failure
 */
static int handle_vhd_status(AVFormatContext *avctx, VHD_ERRORCODE vhd_status,
                             const char *success_message,
                             const char *error_message);

/**
 * @brief Initialize the audio info structure
 *
 * This function initializes the audio info structure
 * with default values and sets the codec ID to AV_CODEC_ID_NONE.
 * @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param audio_info  Pointer to the variable to store the audio info
 * @return int 0 on success, negative AVERROR code on failure
 */
static int init_audio_info(VideoMasterContext *videomaster_context,
                           VHD_AUDIOINFO      *audio_info);

/**
 * @brief Convert the interleaved audio info to an audio buffer
 * This function converts the interleaved audio info
 * to an audio buffer
 * and stores the pointer to the buffer
 * in the videomaster_context context.
 *  @param videomaster_context  VideoMasterContext pointer to the
 * VideoMasterContext
 * @param audio_info  Pointer to the variable to store the audio info
 * @param audio_buffer  Pointer to the variable to store the audio buffer
 * @param audio_buffer_size  Pointer to the variable to store the audio buffer
 * size
 * @return int 0 on success, negative AVERROR code on failure
 */

static int interleaved_audio_info_to_audio_buffer(
    VideoMasterContext *videomaster_context, VHD_AUDIOINFO *audio_info,
    uint8_t **audio_buffer, uint32_t *audio_buffer_size);

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
int add_device_info_into_list(VideoMasterContext *videomaster_context,
                              char *board_name, char *serial_number,
                              struct AVDeviceInfoList **device_list)
{
    char          error_msg[128];
    int           av_error = 0;
    const char   *device_name = NULL;
    const char   *device_description = NULL;
    AVDeviceInfo *new_device = NULL;
    snprintf(error_msg, sizeof(error_msg),
             "Failed to get stream properties for channel %d on board %d",
             videomaster_context->channel_index,
             videomaster_context->board_index);

    GET_AND_CHECK(
        handle_av_error, videomaster_context->avctx, videomaster_context->avctx,
        ff_videomaster_get_video_stream_properties(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->stream_handle,
            videomaster_context->channel_index,
            &videomaster_context->channel_type,
            &videomaster_context->video_info, &videomaster_context->video_width,
            &videomaster_context->video_height,
            &videomaster_context->video_frame_rate_num,
            &videomaster_context->video_frame_rate_den,
            &videomaster_context->video_interlaced),
        "", error_msg);

    snprintf(error_msg, sizeof(error_msg),
             "Failed to get audio stream properties for channel %d on board "
             "%d",
             videomaster_context->channel_index,
             videomaster_context->board_index);

    GET_AND_CHECK(handle_av_error, videomaster_context->avctx,
                  videomaster_context->avctx,
                  ff_videomaster_get_audio_stream_properties(
                      videomaster_context->avctx,
                      videomaster_context->board_handle,
                      videomaster_context->stream_handle,
                      videomaster_context->channel_index,
                      &videomaster_context->channel_type,
                      &videomaster_context->audio_info,
                      &videomaster_context->audio_sample_rate,
                      &videomaster_context->audio_nb_channels,
                      &videomaster_context->audio_sample_size,
                      &videomaster_context->audio_codec),
                  "", error_msg);

    device_name = format_device_name(videomaster_context, board_name,
                                     serial_number);

    device_description = format_device_description(videomaster_context,
                                                   board_name, serial_number);
    if (!device_name || !device_description)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for device name or description for "
               "channel %d on board %d\n",
               videomaster_context->channel_index,
               videomaster_context->board_index);
        return AVERROR(ENOMEM);
    }

    new_device = create_device_info(videomaster_context, (char *)device_name,
                                    (char *)device_description, true);

    if (!new_device)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to create device info for channel %d on "
               "board %d\n",
               videomaster_context->channel_index,
               videomaster_context->board_index);
        av_freep(&device_name);
        av_freep(&device_description);
        return AVERROR(ENOMEM);
    }

    av_log(videomaster_context->avctx, AV_LOG_DEBUG,
           "Device info created for channel %d on board %d : device_name = "
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
           "Device info for channel %d on board %d added to list\n",
           videomaster_context->channel_index,
           videomaster_context->board_index);

    return av_error;
}

AVDeviceInfo *create_device_info(VideoMasterContext *videomaster_context,
                                 char *device_name, char *device_description,
                                 bool is_video)
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

int disable_loopback_on_channel(VideoMasterContext *videomaster_context)
{
    uint32_t has_passive_loopback = false;
    uint32_t has_active_loopback = false;

    handle_vhd_status(
        videomaster_context->avctx,
        VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_PASSIVE_LOOPBACK,
                               &has_passive_loopback),
        "", "");
    handle_vhd_status(videomaster_context->avctx,
                      VHD_GetBoardCapability(videomaster_context->board_handle,
                                             VHD_CORE_BOARD_CAP_ACTIVE_LOOPBACK,
                                             &has_active_loopback),
                      "", "");

    if (has_active_loopback &&
        get_active_loopback_property(videomaster_context->channel_index) !=
            NB_VHD_CORE_BOARDPROPERTIES)
    {
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetBoardProperty(videomaster_context->board_handle,
                                 get_active_loopback_property(
                                     videomaster_context->channel_index),
                                 false),
            "", "");
    }

    if (has_passive_loopback &&
        get_passive_loopback_property(videomaster_context->channel_index) !=
            NB_VHD_CORE_BOARDPROPERTIES)
    {
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetBoardProperty(videomaster_context->board_handle,
                                 get_passive_loopback_property(
                                     videomaster_context->channel_index),
                                 false),
            "", "");
    }

    return 0;
}

char *format_device_description(VideoMasterContext *videomaster_context,
                                const char         *board_name,
                                const char         *serial_number)
{
    char  *device_description = av_mallocz(256);
    double frame_rate = 0.0;
    if (!device_description)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for device description\n");
        return NULL;
    }

    frame_rate = (double)videomaster_context->video_frame_rate_num /
                 videomaster_context->video_frame_rate_den;
    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {
        snprintf(
            device_description, 256,
            "HDMI video: %dx%d%s%.3f %s %s, audio: %d channels @%dHz (%d bits) "
            "on "
            "board %s (SN: %s)",
            videomaster_context->video_width, videomaster_context->video_height,
            videomaster_context->video_interlaced ? "i" : "p", frame_rate,
            VHD_DV_CS_ToPrettyString(
                videomaster_context->video_info.hdmi.color_space),
            VHD_DV_SAMPLING_ToPrettyString(
                videomaster_context->video_info.hdmi.cable_bit_sampling),
            videomaster_context->audio_nb_channels,
            videomaster_context->audio_sample_rate,
            videomaster_context->audio_sample_size, board_name, serial_number);
    }
    else
    {
        const char *interface_str = VHD_INTERFACE_ToPrettyString(
            videomaster_context->video_info.sdi.interface);

        snprintf(device_description, 256,
                 "SDI video: %dx%d%s%.3f (interface: %s) on board %s (SN: %s)",
                 videomaster_context->video_width,
                 videomaster_context->video_height,
                 videomaster_context->video_interlaced ? "i" : "p", frame_rate,
                 interface_str, board_name, serial_number);
    }

    return device_description;
}

char *format_device_name(VideoMasterContext *videomaster_context,
                         const char *board_name, const char *serial_number)
{
    char *device_name = av_mallocz(256);
    if (!device_name)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for device name\n");
        return NULL;
    }
    snprintf(device_name, 256, "stream %d on board %d",
             videomaster_context->channel_index,
             videomaster_context->board_index);
    return device_name;
}

VHD_CORE_BOARDPROPERTY get_active_loopback_property(int channel_index)
{
    switch (channel_index)
    {
    case 0:
        return VHD_CORE_BP_ACTIVE_LOOPBACK_0;
    default:
        return NB_VHD_CORE_BOARDPROPERTIES;
    }
}

int get_audio_buffer(VideoMasterContext *videomaster_context)
{

    VHD_DV_AUDIO_TYPE      audio_type;
    VHD_DV_AUDIO_INFOFRAME audio_infoframe;
    VHD_DV_AUDIO_AES_STS   audio_aes_status;
    VHD_AUDIOINFO         *audio_info;

    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {

        if (handle_vhd_status(videomaster_context->avctx,
                              VHD_GetSlotDvAudioInfo(
                                  videomaster_context->slot_handle, &audio_type,
                                  &audio_infoframe, &audio_aes_status),
                              "Audio slot buffer retrieved successfully",
                              "Failed to retrieve audio slot buffer") != 0)
            return AVERROR(EIO);

        if (audio_type != VHD_DV_AUDIO_TYPE_NONE)
        {
            videomaster_context->audio_buffer_size = 0;
            if (audio_aes_status.LinearPCM ==
                VHD_DV_AUDIO_AES_SAMPLE_STS_LINEAR_PCM_SAMPLE)
            {
                handle_vhd_status(
                    videomaster_context->avctx,
                    VHD_SlotExtractDvPCMAudio(
                        videomaster_context->slot_handle,
                        videomaster_context->audio_info.hdmi.format,
                        get_channel_mask_from_nb_channels(videomaster_context),
                        NULL, &videomaster_context->audio_buffer_size),
                    "", "");
                videomaster_context->audio_buffer = av_mallocz(
                    videomaster_context->audio_buffer_size);
                if (handle_vhd_status(
                        videomaster_context->avctx,
                        VHD_SlotExtractDvPCMAudio(
                            videomaster_context->slot_handle,
                            videomaster_context->audio_info.hdmi.format,
                            get_channel_mask_from_nb_channels(
                                videomaster_context),
                            videomaster_context->audio_buffer,
                            &videomaster_context->audio_buffer_size),
                        "Audio slot buffer retrieved successfully",
                        "Failed to retrieve audio slot buffer") != 0)
                {
                    av_freep(&videomaster_context->audio_buffer);
                    return AVERROR(EIO);
                }
            }
            else
            {
                av_log(videomaster_context->avctx, AV_LOG_ERROR,
                       "Non PCM audio is not supported\n");
                return AVERROR(EIO);
            }
        }
        else
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "No audio type detected in audio InfoFrame or stream "
                   "header.\n");
            return AVERROR(EIO);
        }
    }
    else
    {
        audio_info = &videomaster_context->audio_info.sdi.audio_info;
        handle_vhd_status(videomaster_context->avctx,
                          VHD_SlotExtractAudio(videomaster_context->slot_handle,
                                               audio_info),
                          "Audio slot buffer retrieved successfully",
                          "Failed to retrieve audio slot buffer");
        interleaved_audio_info_to_audio_buffer(
            videomaster_context, audio_info, &videomaster_context->audio_buffer,
            &videomaster_context->audio_buffer_size);
    }
    return 0;
}

int get_audio_stream_properties_from_audio_infoframe(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, union VideoMasterAudioInfo *audio_info,
    uint32_t *sample_rate, uint32_t *nb_channels, uint32_t *sample_size,
    enum AVCodecID *codec)
{
    VHD_DV_AUDIO_TYPE      audio_type = VHD_DV_AUDIO_TYPE_NONE;
    VHD_DV_AUDIO_INFOFRAME audio_info_frame = { 0 };
    VHD_DV_AUDIO_AES_STS   audio_aes_status = { 0 };
    int                    av_error = 0;

    VideoMasterContext videomaster_context_struct = {
        .avctx = avctx,
        .board_handle = board_handle,
        .stream_handle = stream_handle,
        .channel_index = channel_index,
        .channel_type = AV_VIDEOMASTER_CHANNEL_HDMI,
    };

    VideoMasterContext *videomaster_context = &videomaster_context_struct;

    GET_AND_CHECK(
        handle_av_error, videomaster_context->avctx, videomaster_context->avctx,
        ff_videomaster_get_video_stream_properties(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->stream_handle,
            videomaster_context->channel_index,
            &videomaster_context->channel_type,
            &videomaster_context->video_info, &videomaster_context->video_width,
            &videomaster_context->video_height,
            &videomaster_context->video_frame_rate_num,
            &videomaster_context->video_frame_rate_den,
            &videomaster_context->video_interlaced),
        "Video stream properties retrieved successfully",
        "Could not retrieve video stream properties");

    *audio_info = (union VideoMasterAudioInfo){ 0 };
    *codec = AV_CODEC_ID_NONE;

    GET_AND_CHECK(ff_videomaster_start_stream, avctx, videomaster_context);

    GET_AND_CHECK_AND_STOP_STREAM(lock_slot, avctx, videomaster_context);

    GET_AND_CHECK_AND_STOP_STREAM(
        handle_vhd_status, avctx, avctx,
        VHD_GetSlotDvAudioInfo(videomaster_context->slot_handle, &audio_type,
                               &audio_info_frame, &audio_aes_status),
        "Audio info frame retrieved successfully",
        "Failed to retrieve audio info frame");

    if (audio_type == VHD_DV_AUDIO_TYPE_NONE)
    {
        av_log(avctx, AV_LOG_TRACE, "No audio detected\n");
        ff_videomaster_stop_stream(videomaster_context);
        return 0;
    }

    GET_AND_CHECK_AND_STOP_STREAM(
        handle_av_error, avctx, avctx,
        get_sample_size_from_audio_infoframe_and_aes_status(videomaster_context,
                                                            audio_info_frame,
                                                            audio_aes_status,
                                                            sample_size),
        "", "Failed to get audio bits per sample from audio info frame");

    GET_AND_CHECK_AND_STOP_STREAM(
        handle_av_error, avctx, avctx,
        get_sample_rate_from_audio_infoframe_and_aes_status(videomaster_context,
                                                            audio_info_frame,
                                                            audio_aes_status,
                                                            sample_rate),
        "", "Failed to get audio sample rate from audio info frame");
    GET_AND_CHECK_AND_STOP_STREAM(
        handle_av_error, avctx, avctx,
        get_nb_channels_from_audio_infoframe_and_aes_status(videomaster_context,
                                                            audio_info_frame,
                                                            audio_aes_status,
                                                            nb_channels),
        "", "Failed to get audio channels from audio info frame");

    GET_AND_CHECK_AND_STOP_STREAM(handle_av_error, avctx, avctx,
                                  get_codec_from_audio_infoframe_and_aes_status(
                                      videomaster_context, audio_info_frame,
                                      audio_aes_status, codec),
                                  "", "Unsupported non PCM audio format");

    if (*sample_size == 16)
        audio_info->hdmi.format = VHD_DVAUDIOFORMAT_16;
    else if (*sample_size == 24 || *sample_size == 20)
        audio_info->hdmi.format = VHD_DVAUDIOFORMAT_24;

    GET_AND_CHECK_AND_STOP_STREAM(unlock_slot, avctx, videomaster_context);

    GET_AND_CHECK(ff_videomaster_stop_stream, avctx, videomaster_context);

    return av_error;
}

int get_board_name_and_serial_number(VideoMasterContext *videomaster_context,
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

int get_board_name(VideoMasterContext *videomaster_context,
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

int get_channel_mask_from_nb_channels(VideoMasterContext *videomaster_context)
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
               "Unsupported number of channels: %d\n",
               videomaster_context->audio_nb_channels);
        return AVERROR(EINVAL);
    }
}

int get_codec_from_audio_infoframe_and_aes_status(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    enum AVCodecID *codec_id)
{
    int      return_code = 0;
    uint32_t sample_size = 0;
    switch (audio_info_frame.CodingType)
    {
    case VHD_DV_AUDIO_INFOFRAME_CODING_TYPE_PCM:
        get_sample_size_from_audio_infoframe_and_aes_status(videomaster_context,
                                                            audio_info_frame,
                                                            aes_status,
                                                            &sample_size);
        if (sample_size == 16)
            *codec_id = AV_CODEC_ID_PCM_S16LE;
        else if (sample_size == 24)
            *codec_id = AV_CODEC_ID_PCM_S24LE;
        else if (sample_size == 20)
            *codec_id = AV_CODEC_ID_PCM_S24LE;  // 20 bits is not supported by
                                                // FFmpeg, so we use 24 bits
                                                // with  deltacast conversion
        else
            return_code = AVERROR(EINVAL);
        break;
    case VHD_DV_AUDIO_INFOFRAME_CODING_TYPE_REF_STREAM_HEADER:
        switch (aes_status.LinearPCM)
        {
        case VHD_DV_AUDIO_AES_SAMPLE_STS_LINEAR_PCM_SAMPLE:
            get_sample_size_from_audio_infoframe_and_aes_status(
                videomaster_context, audio_info_frame, aes_status,
                &sample_size);
            if (sample_size == 16)
                *codec_id = AV_CODEC_ID_PCM_S16LE;
            else if (sample_size == 24)
                *codec_id = AV_CODEC_ID_PCM_S24LE;
            else if (sample_size == 20)
                *codec_id =
                    AV_CODEC_ID_PCM_S24LE;  // 20 bits is not supported by
                                            // FFmpeg, so we use 24 bits
                                            // with deltacast conversion
            else
                return_code = AVERROR(EINVAL);
            break;
        default:
            av_log(videomaster_context->avctx, AV_LOG_WARNING,
                   "Not implemented audio codec type - Non Linear PCM in AES  "
                   "STATUS.\n");
            return_code = AVERROR(EINVAL);
            break;
        }
        break;

    default:
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Not implemented audio codec type: %08X\n",
               audio_info_frame.CodingType);
        return_code = AVERROR(EINVAL);
        break;
    }
    return return_code;
}

VHD_CORE_BOARDPROPERTY get_firmware_loopback_property(int channel_index)
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
int get_nb_channels_from_audio_infoframe_and_aes_status(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    uint32_t *nb_channels)
{
    // conversion is based on the table provided in CTA-861-I specification,
    // section 6.6.1
    int return_code = 0;
    switch (audio_info_frame.ChannelCount)
    {
    case VHD_DV_AUDIO_INFOFRAME_CHANNEL_COUNT_2:
        *nb_channels = 2;
        break;
    case VHD_DV_AUDIO_INFOFRAME_CHANNEL_COUNT_3:
        *nb_channels = 3;
        break;
    case VHD_DV_AUDIO_INFOFRAME_CHANNEL_COUNT_4:
        *nb_channels = 4;
        break;
    case VHD_DV_AUDIO_INFOFRAME_CHANNEL_COUNT_5:
        *nb_channels = 5;
        break;
    case VHD_DV_AUDIO_INFOFRAME_CHANNEL_COUNT_6:
        *nb_channels = 6;
        break;
    case VHD_DV_AUDIO_INFOFRAME_CHANNEL_COUNT_7:
        *nb_channels = 7;
        break;
    case VHD_DV_AUDIO_INFOFRAME_CHANNEL_COUNT_8:
        *nb_channels = 8;
        break;
    case VHD_DV_AUDIO_INFOFRAME_CHANNEL_COUNT_REF_STREAM_HEADER:
        *nb_channels = aes_status.ChannelNb;
        break;
    default:
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Unsupported audio channel count in audio InfoFrame: %08X\n",
               audio_info_frame.ChannelCount);
        return_code = AVERROR(EINVAL);
        break;
    }
    return return_code;
}

VHD_STREAMTYPE get_rx_stream_type_from_index(uint32_t index)
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

VHD_CORE_BOARDPROPERTY get_passive_loopback_property(int channel_index)
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
int get_sample_rate_from_audio_infoframe_and_aes_status(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    uint32_t *sample_rate)
{
    // conversion is based on the table provided in CTA-861-I specification,
    // section 6.6.1
    int return_code = 0;
    switch (audio_info_frame.SamplingFrequency)
    {
    case VHD_DV_AUDIO_INFOFRAME_SAMPLING_FREQ_32000HZ:
        *sample_rate = 32000;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLING_FREQ_44100HZ:
        *sample_rate = 44100;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLING_FREQ_48000HZ:
        *sample_rate = 48000;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLING_FREQ_88200HZ:
        *sample_rate = 88200;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLING_FREQ_96000HZ:
        *sample_rate = 96000;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLING_FREQ_176400HZ:
        *sample_rate = 176400;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLING_FREQ_192000HZ:
        *sample_rate = 192000;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLING_FREQ_REF_STREAM_HEADER:
        switch (aes_status.SamplingFrequency)
        {
        case VHD_DV_AUDIO_AES_STS_SAMPLING_FREQ_32000HZ:
            *sample_rate = 32000;
            break;
        case VHD_DV_AUDIO_AES_STS_SAMPLING_FREQ_44100HZ:
            *sample_rate = 44100;
            break;
        case VHD_DV_AUDIO_AES_STS_SAMPLING_FREQ_48000HZ:
            *sample_rate = 48000;
            break;
        case VHD_DV_AUDIO_AES_STS_SAMPLING_FREQ_88200HZ:
            *sample_rate = 88200;
            break;
        case VHD_DV_AUDIO_AES_STS_SAMPLING_FREQ_96000HZ:
            *sample_rate = 96000;
            break;
        case VHD_DV_AUDIO_AES_STS_SAMPLING_FREQ_176000HZ:
            *sample_rate = 176400;
            break;
        case VHD_DV_AUDIO_AES_STS_SAMPLING_FREQ_192000HZ:
            *sample_rate = 192000;
            break;
        default:
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Unsupported audio sample rate in AES Status: %08X\n",
                   aes_status.SamplingFrequency);
            return_code = AVERROR(EINVAL);
            break;
        }
        break;
    default:
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Unsupported audio sample rate in audio InfoFrame: %08X\n",
               audio_info_frame.SamplingFrequency);
        return_code = AVERROR(EINVAL);
        break;
    }
    return return_code;
}

int get_sample_size_from_audio_infoframe_and_aes_status(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    uint32_t *sample_size)
{
    // conversion is based on the table provided in CTA-861-I specification,
    // section 6.6.1
    int return_code = 0;
    switch (audio_info_frame.SampleSize)
    {
    case VHD_DV_AUDIO_INFOFRAME_SAMPLE_SIZE_16_BITS:
        *sample_size = 16;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLE_SIZE_20_BITS:
        *sample_size = 20;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLE_SIZE_24_BITS:
        *sample_size = 24;
        break;
    case VHD_DV_AUDIO_INFOFRAME_SAMPLE_SIZE_REF_STREAM_HEADER:
        switch (aes_status.MaxWordLengthSize)
        {
        case VHD_DV_AUDIO_AES_STS_MAX_WORD_LENGTH_20BITS:
            *sample_size = 20;
            break;
        case VHD_DV_AUDIO_AES_STS_MAX_WORD_LENGTH_24BITS:
            *sample_size = 24;
            break;
        default:
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Unsupported audio bits per sample in AES Status: %08X\n",
                   aes_status.MaxWordLengthSize);
            return_code = AVERROR(EINVAL);
            break;
        }
        break;
    default:
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Unsupported audio bits per sample in audio InfoFrame: %08X\n",
               audio_info_frame.SampleSize);
        return_code = AVERROR(EINVAL);
        break;
    }
    return return_code;
}

int get_serial_number(VideoMasterContext *videomaster_context,
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
        if (handle_vhd_status(videomaster_context->avctx,
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

int get_video_buffer(VideoMasterContext *videomaster_context)
{
    uint32_t video_buffer_type = VHD_DV_BT_VIDEO;

    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {
        video_buffer_type = VHD_DV_BT_VIDEO;
    }
    else
    {
        video_buffer_type = VHD_SDI_BT_VIDEO;
    }
    return handle_vhd_status(
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

int handle_av_error(AVFormatContext *avctx, int av_error,
                    const char *trace_message, const char *error_message)
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

int handle_vhd_status(AVFormatContext *avctx, VHD_ERRORCODE vhd_status,
                      const char *success_message, const char *error_message)
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

int init_audio_info(VideoMasterContext *videomaster_context,
                    VHD_AUDIOINFO      *audio_info)
{
    VHD_AUDIOGROUP   *audio_group = NULL;
    VHD_AUDIOCHANNEL *audio_channel = NULL;
    VHD_AUDIOFORMAT   buffer_format = videomaster_context->audio_sample_size ==
                                            AV_VIDEOMASTER_SAMPLE_SIZE_16
                                          ? VHD_AF_16
                                          : VHD_AF_24;
    uint32_t          channel_count = 0;
    uint32_t          nb_samples = VHD_GetNbSamples(
        videomaster_context->video_info.sdi.video_standard,
        videomaster_context->video_info.sdi.clock_divisor, VHD_ASR_48000, 0);
    uint32_t nb_channels = videomaster_context->audio_nb_channels;

    memset(audio_info, 0, sizeof(VHD_AUDIOINFO));

    for (int audio_group_idx = 0;
         audio_group_idx < VHD_NBOFGROUP && channel_count < nb_channels;
         audio_group_idx++)
    {
        for (int channel_idx = 0;
             channel_idx < VHD_NBOFCHNPERGROUP && channel_count < nb_channels;
             channel_idx++, channel_count++)
        {
            audio_group = &audio_info->pAudioGroups[audio_group_idx];
            audio_channel = &audio_group->pAudioChannels[channel_idx];

            // set mode: stereo if at least 2 channels remain, else mono
            audio_channel->Mode = (nb_channels - channel_count) <= 1 &&
                                          (nb_channels % 2 == 1)
                                      ? VHD_AM_MONO
                                      : VHD_AM_STEREO;
            audio_channel->BufferFormat = buffer_format;
            if (channel_idx % 2 == 0)
            {
                audio_channel->DataSize =
                    nb_samples * VHD_GetBlockSize(audio_channel->BufferFormat,
                                                  audio_channel->Mode);
                audio_channel->pData = av_malloc(audio_channel->DataSize);
                if (!audio_channel->pData)
                {
                    av_log(videomaster_context->avctx, AV_LOG_ERROR,
                           "Failed to allocate memory for audio channel "
                           "%d in group %d\n",
                           channel_idx, audio_group_idx);
                    return AVERROR(ENOMEM);
                }
            }
        }
    }
    return 0;
}

int interleaved_audio_info_to_audio_buffer(
    VideoMasterContext *videomaster_context, VHD_AUDIOINFO *audio_info,
    uint8_t **audio_buffer, uint32_t *audio_buffer_size)
{

    uint8_t *channel_buffers[VHD_NBOFGROUP * VHD_NBOFCHNPERGROUP / 2] = { 0 };
    uint32_t channel_sizes[VHD_NBOFGROUP * VHD_NBOFCHNPERGROUP / 2] = { 0 };
    VHD_AUDIOMODE channel_modes[VHD_NBOFGROUP * VHD_NBOFCHNPERGROUP / 2] = {
        0
    };
    uint32_t nb_channels = videomaster_context->audio_nb_channels;
    uint32_t bytes_per_sample = videomaster_context->audio_sample_size / 8;
    uint32_t nb_samples = 0;
    uint32_t channel_pair_count = 0;
    uint32_t samples_in_channel = 0;
    uint32_t dst_offset = 0;
    uint32_t src_offset = 0;
    uint32_t channel_pair_index = 0;
    uint8_t *src = NULL;
    uint8_t *dst = NULL;
    uint8_t *src_ptr = NULL;
    uint8_t *dst_ptr = NULL;
    uint32_t mode = 0;
    uint32_t size = 0;
    if (audio_info == NULL || audio_buffer == NULL || audio_buffer_size == NULL)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Invalid parameters for interleaved audio info to audio buffer "
               "conversion\n");
        return AVERROR(EINVAL);
    }
    // Gather pointers, sizes, and modes for all channels
    for (int group = 0;
         group < VHD_NBOFGROUP && channel_pair_count < nb_channels; group++)
    {
        for (int even_channel_idx = 0;
             even_channel_idx < VHD_NBOFCHNPERGROUP / 2 &&
             channel_pair_count < nb_channels;
             even_channel_idx++, channel_pair_count++)
        {
            int               channel_index = even_channel_idx * 2;
            VHD_AUDIOCHANNEL *audio_channel =
                &audio_info->pAudioGroups[group].pAudioChannels[channel_index];
            channel_buffers[channel_pair_count] = audio_channel->pData;
            channel_sizes[channel_pair_count] = audio_channel->DataSize;
            channel_modes[channel_pair_count] = audio_channel->Mode;
            samples_in_channel = 0;
            if (audio_channel->Mode == VHD_AM_STEREO)
                samples_in_channel = audio_channel->DataSize /
                                     (2 * bytes_per_sample);
            else
                samples_in_channel = audio_channel->DataSize / bytes_per_sample;
            if (samples_in_channel > nb_samples)
                nb_samples = samples_in_channel;
        }
    }

    videomaster_context->audio_buffer_size = nb_samples * nb_channels *
                                             bytes_per_sample;
    videomaster_context->audio_buffer = av_mallocz(
        videomaster_context->audio_buffer_size);
    if (!videomaster_context->audio_buffer)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for audio buffer\n");
        return AVERROR(ENOMEM);
    }

    // Interleave all channels, handling STEREO/MONO layout
    for (uint32_t sample_index = 0; sample_index < nb_samples; sample_index++)
    {
        dst_offset = sample_index * nb_channels * bytes_per_sample;
        channel_pair_index = 0;
        for (uint32_t channel_index = 0; channel_index < nb_channels;)
        {
            src = channel_buffers[channel_pair_index];
            mode = channel_modes[channel_pair_index];
            size = channel_sizes[channel_pair_index];
            if (mode == VHD_AM_STEREO)
            {
                for (int lr_channel = 0;
                     lr_channel < 2 && channel_index < nb_channels;
                     lr_channel++, channel_index++)
                {
                    src_offset = (sample_index * 2 + lr_channel) *
                                 bytes_per_sample;
                    src_ptr = NULL;
                    if (src && src_offset + bytes_per_sample <= size)
                        src_ptr = src + src_offset;
                    dst_ptr = (uint8_t *)videomaster_context->audio_buffer +
                              dst_offset + channel_index * bytes_per_sample;
                    if (src_ptr)
                        memcpy(dst_ptr, src_ptr, bytes_per_sample);
                }
            }
            else
            {
                src_offset = sample_index * bytes_per_sample;
                src_ptr = NULL;
                if (src && src_offset + bytes_per_sample <= size)
                    src_ptr = src + src_offset;
                dst_ptr = (uint8_t *)videomaster_context->audio_buffer +
                          dst_offset + channel_index * bytes_per_sample;
                if (src_ptr)
                    memcpy(dst_ptr, src_ptr, bytes_per_sample);
                channel_index++;
            }
            channel_pair_index++;
        }
    }
    return 0;
}

int lock_slot(VideoMasterContext *videomaster_context)
{
    return handle_vhd_status(
        videomaster_context->avctx,
        VHD_LockSlotHandle(videomaster_context->stream_handle,
                           &videomaster_context->slot_handle),
        "Slot handle locked successfully", "Failed to lock slot handle");
}

int unlock_slot(VideoMasterContext *videomaster_context)
{
    int return_code = 0;
    if (videomaster_context->slot_handle)
    {
        return_code = handle_vhd_status(videomaster_context->avctx,
                                        VHD_UnlockSlotHandle(
                                            videomaster_context->slot_handle),
                                        "Slot handle unlocked successfully",
                                        "Failed to unlock slot handle");
        videomaster_context->slot_handle = NULL;
    }
    return return_code;
}

int release_audio_info(VideoMasterContext *videomaster_context,
                       VHD_AUDIOINFO      *audio_info)
{
    if (audio_info == NULL)
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Audio info is NULL, nothing to release\n");
        return 0;
    }

    if (videomaster_context->has_audio &&
        (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_SDI ||
         videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI))
        for (int audio_group_idx = 0; audio_group_idx < VHD_NBOFGROUP;
             audio_group_idx++)
            for (int channel_index = 0; channel_index < VHD_NBOFCHNPERGROUP;
                 channel_index++)
            {
                VHD_AUDIOCHANNEL *audio_channel =
                    &audio_info->pAudioGroups[audio_group_idx]
                         .pAudioChannels[channel_index];
                if (audio_channel->pData)
                {
                    av_log(videomaster_context->avctx, AV_LOG_TRACE,
                           "Freeing audio data buffer of size %d\n",
                           audio_channel->DataSize);
                    av_freep(&audio_channel->pData);
                    audio_channel->pData = NULL;
                }
            }
    return 0;
}

/** functions definitions */

int ff_videomaster_close_board_handle(VideoMasterContext *videomaster_context)
{
    int return_code = handle_vhd_status(videomaster_context->avctx,
                                        VHD_CloseBoardHandle(
                                            videomaster_context->board_handle),
                                        "Board handle closed successfully",
                                        "Failed to close board handle");
    videomaster_context->board_handle = NULL;
    return return_code;
}

int ff_videomaster_close_stream_handle(VideoMasterContext *videomaster_context)
{
    int return_code = handle_vhd_status(videomaster_context->avctx,
                                        VHD_CloseStreamHandle(
                                            videomaster_context->stream_handle),
                                        "Stream handle closed successfully",
                                        "Failed to close stream handle");
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

    av_error = handle_av_error(
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
        if (ff_videomaster_is_channel_locked(videomaster_context))
        {
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "Channel %d is locked on "
                   "board %d -> create "
                   "device info\n",
                   channel_index, board_index);

            av_error = add_device_info_into_list(videomaster_context,
                                                 board_name, serial_number,
                                                 device_list);
            if (av_error != 0)
                break;
        }
        else
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "Channel %d is unlocked "
                   "on board %d\n",
                   channel_index, board_index);
    }
    ff_videomaster_close_board_handle(videomaster_context);
    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_create_devices_"
           "infos_from_board_index: OUT\n");

    return 0;
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
    if (handle_vhd_status(
            videomaster_context->avctx,
            VHD_GetApiInfo(&videomaster_context->api_version,
                           &videomaster_context->number_of_boards),
            "API version retrieved "
            "successfully",
            "Failed to retrieve API "
            "version") == 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_INFO, "API Version: %d\n",
               videomaster_context->api_version);
        av_log(videomaster_context->avctx, AV_LOG_INFO,
               "Number of Boards: %d\n", videomaster_context->number_of_boards);

        av_error = 0;
    }
    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_get_api_info: OUT\n");
    return av_error;
}

int ff_videomaster_get_audio_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterChannelType *channel_type,
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
            handle_vhd_status(avctx,
                              VHD_OpenStreamHandle(
                                  board_handle,
                                  get_rx_stream_type_from_index(channel_index),
                                  VHD_DV_STPROC_JOINED, NULL,
                                  &local_stream_handle, NULL),
                              "Stream handle opened "
                              "successfully",
                              "Failed to open stream "
                              "handle");

        GET_AND_CHECK(handle_av_error, avctx, avctx,
                      get_audio_stream_properties_from_audio_infoframe(
                          avctx, board_handle, local_stream_handle,
                          channel_index, audio_info, sample_rate, nb_channels,
                          sample_size, codec),
                      "Get audio properties",
                      "Failed to get audio stream "
                      "properties");

        if (stream_handle == NULL)
            handle_vhd_status(avctx, VHD_CloseStreamHandle(local_stream_handle),
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

    handle_vhd_status(avctx,
                      VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL,
                                             channel_index, VHD_CORE_CP_TYPE,
                                             (uint32_t *)&channel_type),
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
    default:
        break;
    }

    return AV_VIDEOMASTER_CHANNEL_UNKNOWN;
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
    return handle_vhd_status(
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

int ff_videomaster_get_nb_tx_channels(VideoMasterContext *videomaster_context)
{
    return handle_vhd_status(
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
                                 uint64_t           *timestamp)
{
    int             av_error = 0;
    uint32_t        clock_frequency = 0;
    static uint64_t system_ts_base = 0;
    VHD_TIMECODE    time_code;
    float           total_frames = 0;
    if (videomaster_context->slot_handle == NULL)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Slot handle is NULL, cannot "
               "get timestamp\n");
        return AVERROR(EINVAL);
    }

    if (videomaster_context->timestamp_source ==
        AV_VIDEOMASTER_TIMESTAMP_HARDWARE)
    {
        GET_AND_CHECK(
            handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_GetSlotHardwareTimestamp(videomaster_context->slot_handle,
                                         timestamp, &clock_frequency),
            "Hardware Timestamp retrieved "
            "successfully",
            "Failed to retrieve hardware "
            "timestamp");
        *timestamp = (*timestamp * 1000000) / clock_frequency;
        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "Hardware timestamp: %lli\n", *timestamp);
    }
    else if (videomaster_context->timestamp_source ==
                 AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD ||
             videomaster_context->timestamp_source ==
                 AV_VIDEOMASTER_TIMESTAMP_LTC_COMPANION_CARD)
    {
        GET_AND_CHECK(
            handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_GetSlotTimecode(
                videomaster_context->slot_handle,
                (VHD_TIMECODE_SOURCE)
                    get_videomaster_enumeration_value_for_timestamp_source(
                        videomaster_context->timestamp_source),
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
    else
    {

        GET_AND_CHECK(handle_vhd_status, videomaster_context->avctx,
                      videomaster_context->avctx,
                      VHD_GetSlotSystemTime(videomaster_context->slot_handle,
                                            timestamp),
                      "Timestamp retrieved "
                      "successfully",
                      "Failed to retrieve timestamp");
        // Normalize system timestamp to start
        // at zero
        if (system_ts_base == 0)
            system_ts_base = *timestamp;
        *timestamp -= system_ts_base;
        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "System timestamp: %lli\n", *timestamp);
    }

    return 0;
}

int ff_videomaster_get_video_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterChannelType *channel_type,
    union VideoMasterVideoInfo *video_info, uint32_t *width, uint32_t *height,
    uint32_t *frame_rate_num, uint32_t *frame_rate_den, bool *interlaced)
{
    uint32_t frame_rate = 0;
    uint32_t total_width = 0;
    uint32_t total_height = 0;
    HANDLE   local_stream_handle = stream_handle;
    *channel_type = ff_videomaster_get_channel_type_from_index(avctx,
                                                               board_handle,
                                                               channel_index);
    av_log(avctx, AV_LOG_TRACE,
           "ff_videomaster_get_video_stream_"
           "properties: IN\n");

    if (*channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {

        if (local_stream_handle == NULL)
            handle_vhd_status(avctx,
                              VHD_OpenStreamHandle(
                                  board_handle,
                                  get_rx_stream_type_from_index(channel_index),
                                  VHD_DV_STPROC_JOINED, NULL,
                                  &local_stream_handle, NULL),
                              "Stream handle opened "
                              "successfully",
                              "Failed to open stream "
                              "handle");
        handle_vhd_status(avctx,
                          VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL,
                                                 channel_index,
                                                 VHD_DV_CP_ACTIVE_WIDTH, width),
                          "", "");
        handle_vhd_status(avctx,
                          VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL,
                                                 channel_index,
                                                 VHD_DV_CP_ACTIVE_HEIGHT,
                                                 height),
                          "", "");
        handle_vhd_status(avctx,
                          VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL,
                                                 channel_index,
                                                 VHD_DV_CP_REFRESH_RATE,
                                                 &frame_rate),
                          "", "");
        handle_vhd_status(avctx,
                          VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL,
                                                 channel_index,
                                                 VHD_DV_CP_PIXEL_CLOCK,
                                                 &video_info->hdmi.pixel_clock),
                          "", "");
        handle_vhd_status(avctx,
                          VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL,
                                                 channel_index,
                                                 VHD_DV_CP_INTERLACED,
                                                 (uint32_t *)interlaced),
                          "", "");
        handle_vhd_status(
            avctx,
            VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                   VHD_DV_CP_CABLE_COLOR_SPACE,
                                   (uint32_t *)&video_info->hdmi.color_space),
            "", "");
        handle_vhd_status(avctx,
                          VHD_GetChannelProperty(
                              board_handle, VHD_RX_CHANNEL, channel_index,
                              VHD_DV_CP_CABLE_BIT_SAMPLING,
                              (uint32_t *)&video_info->hdmi.cable_bit_sampling),
                          "", "");
        handle_vhd_status(avctx,
                          VHD_GetStreamProperty(local_stream_handle,
                                                VHD_DV_SP_TOTAL_WIDTH,
                                                (uint32_t *)&total_width),
                          "", "");
        handle_vhd_status(avctx,
                          VHD_GetStreamProperty(local_stream_handle,
                                                VHD_DV_SP_TOTAL_HEIGHT,
                                                (uint32_t *)&total_height),
                          "", "");

        if (stream_handle == NULL)
            handle_vhd_status(avctx, VHD_CloseStreamHandle(local_stream_handle),
                              "Stream handle closed "
                              "successfully",
                              "Failed to close stream "
                              "handle");
        video_info->hdmi.refresh_rate = frame_rate;
        *frame_rate_num = video_info->hdmi.pixel_clock * 1000;
        *frame_rate_den = total_width * total_height;
    }
    else
    {
        handle_vhd_status(
            avctx,
            VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                   VHD_SDI_CP_VIDEO_STANDARD,
                                   (uint32_t *)&video_info->sdi.video_standard),
            "", "");
        handle_vhd_status(avctx,
                          VHD_GetVideoCharacteristics(
                              video_info->sdi.video_standard, width, height,
                              (BOOL32 *)interlaced, &frame_rate),
                          "", "");

        handle_vhd_status(
            avctx,
            VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                   VHD_SDI_CP_CLOCK_DIVISOR,
                                   (uint32_t *)&video_info->sdi.clock_divisor),
            "", "");

        handle_vhd_status(
            avctx,
            VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                   VHD_SDI_CP_INTERFACE,
                                   (uint32_t *)&video_info->sdi.interface),
            "", "");

        handle_vhd_status(
            avctx,
            VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                   VHD_SDI_CP_GENLOCK_OFFSET,
                                   &video_info->sdi.genlock_offset),
            "", "");

        *frame_rate_num = frame_rate * 1000;
        switch (video_info->sdi.clock_divisor)
        {
        case VHD_CLOCKDIV_1:
            *frame_rate_den = 1000;
            break;
        case VHD_CLOCKDIV_1001:
            *frame_rate_den = 1001;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR,
                   "Unsupported clock "
                   "divisor: %d\n",
                   video_info->sdi.clock_divisor);
        }
    }

    av_log(avctx, AV_LOG_TRACE,
           "ff_videomaster_get_video_stream_"
           "properties: OUT\n");
    return 0;
}

bool ff_videomaster_is_channel_locked(VideoMasterContext *videomaster_context)
{
    bool     channel_locked = false;
    uint32_t channel_status = 0;
    if (handle_vhd_status(videomaster_context->avctx,
                          VHD_GetChannelProperty(
                              videomaster_context->board_handle, VHD_RX_CHANNEL,
                              videomaster_context->channel_index,
                              VHD_CORE_CP_STATUS, &channel_status),
                          "Channel status retrieved "
                          "successfully",
                          "Failed to retrieve channel "
                          "status") == 0)
    {
        channel_locked = !(channel_status & VHD_CORE_RXSTS_UNLOCKED);
    }
    return channel_locked;
}

bool ff_videomaster_is_hardware_timestamp_supported(
    VideoMasterContext *videomaster_context)
{
    bool hardware_timestamp_supported = false;
    if (videomaster_context->board_handle)
        VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_TIMESTAMP,
                               (ULONG *)&hardware_timestamp_supported);
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }
    return hardware_timestamp_supported;
}

bool ff_videomaster_is_ltc_companion_card_present(
    VideoMasterContext *videomaster_context)
{
    bool ltc_companion_card_feature_supported = false;
    bool ltc_companion_card_present = false;
    if (videomaster_context->board_handle)
    {
        if (ff_videomaster_is_ltc_companion_card_supported(videomaster_context))
        {
            VHD_DetectCompanionCard(videomaster_context->board_handle,
                                    VHD_LTC_COMPANION_CARD,
                                    (BOOL32 *)&ltc_companion_card_present);
        }
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }

    return ltc_companion_card_present;
}

bool ff_videomaster_is_ltc_companion_card_supported(
    VideoMasterContext *videomaster_context)
{
    bool ltc_companion_card_feature_supported = false;
    if (videomaster_context->board_handle)
        VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_LTC_COMPANION_CARD,
                               (ULONG *)&ltc_companion_card_feature_supported);
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }
    return ltc_companion_card_feature_supported;
}

bool ff_videomaster_is_ltc_on_board_timestamp_supported(
    VideoMasterContext *videomaster_context)
{
    bool ltc_on_board_timestamp_supported = false;
    if (videomaster_context->board_handle)
        VHD_GetBoardCapability(videomaster_context->board_handle,
                               VHD_CORE_BOARD_CAP_LTC_ONBOARD,
                               (ULONG *)&ltc_on_board_timestamp_supported);
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Board handle is missing\n");
    }

    return ltc_on_board_timestamp_supported;
}

int ff_videomaster_open_board_handle(VideoMasterContext *videomaster_context)
{
    return handle_vhd_status(
        videomaster_context->avctx,
        VHD_OpenBoardHandle(videomaster_context->board_index,
                            &videomaster_context->board_handle, NULL, 0),
        "Board handle opened successfully", "Failed to open board handle");
}

int ff_videomaster_open_stream_handle(VideoMasterContext *videomaster_context)
{
    uint32_t stream_proc = VHD_DV_STPROC_JOINED;

    if (videomaster_context->channel_type != AV_VIDEOMASTER_CHANNEL_HDMI)
        stream_proc = VHD_SDI_STPROC_JOINED;
    // Open stream as JOINED to get audio and
    // video data
    return handle_vhd_status(
        videomaster_context->avctx,
        VHD_OpenStreamHandle(videomaster_context->board_handle,
                             get_rx_stream_type_from_index(
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
               "%d\n",
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

    handle_vhd_status(videomaster_context->avctx,
                      disable_loopback_on_channel(videomaster_context), "", "");

    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_HDMI)
    {
        /* Set the primary mode of this
         * channel to HDMI */
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_DV_SP_MODE, VHD_DV_MODE_HDMI),
            "", "");

        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_DV_SP_ACTIVE_WIDTH,
                                  videomaster_context->video_width),
            "", "");
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_DV_SP_ACTIVE_HEIGHT,
                                  videomaster_context->video_height),
            "", "");
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_DV_SP_INTERLACED,
                                  (ULONG)videomaster_context->video_interlaced),
            "", "");
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(
                videomaster_context->stream_handle, VHD_DV_SP_REFRESH_RATE,
                videomaster_context->video_info.hdmi.refresh_rate),
            "", "");
        handle_vhd_status(videomaster_context->avctx,
                          VHD_SetStreamProperty(
                              videomaster_context->stream_handle,
                              VHD_DV_SP_PIXEL_CLOCK,
                              videomaster_context->video_info.hdmi.pixel_clock),
                          "", "");
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(
                videomaster_context->stream_handle, VHD_DV_SP_CS,
                (ULONG)videomaster_context->video_info.hdmi.color_space),
            "", "");
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(
                videomaster_context->stream_handle,
                VHD_DV_SP_CABLE_BIT_SAMPLING,
                (ULONG)videomaster_context->video_info.hdmi.cable_bit_sampling),
            "", "");
    }
    else
    {
        /* Set the primary mode of this
         * channel to ASI */
        if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI)
            handle_vhd_status(
                videomaster_context->avctx,
                VHD_SetChannelProperty(videomaster_context->board_handle,
                                       VHD_RX_CHANNEL,
                                       videomaster_context->channel_index,
                                       VHD_CORE_CP_MODE, VHD_CHANNEL_MODE_SDI),
                "", "");

        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(
                videomaster_context->stream_handle, VHD_SDI_SP_VIDEO_STANDARD,
                videomaster_context->video_info.sdi.video_standard),
            "", "");
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(
                videomaster_context->stream_handle,
                VHD_SDI_BP_GENLOCK_CLOCK_DIV,
                videomaster_context->video_info.sdi.clock_divisor),
            "", "");
        handle_vhd_status(videomaster_context->avctx,
                          VHD_SetStreamProperty(
                              videomaster_context->stream_handle,
                              VHD_SDI_SP_INTERFACE,
                              videomaster_context->video_info.sdi.interface),
                          "", "");

        if (videomaster_context->has_audio)
            init_audio_info(videomaster_context,
                            &videomaster_context->audio_info.sdi.audio_info);
    }

    handle_vhd_status(videomaster_context->avctx,
                      VHD_SetStreamProperty(videomaster_context->stream_handle,
                                            VHD_CORE_SP_TRANSFER_SCHEME,
                                            VHD_TRANSFER_SLAVED),
                      "", "");

    /* If Line padding property is supported,
     * use V210 Video decoder. Otherwise, use
     * YUV422 8bits.*/
    if (VHD_SetStreamProperty(videomaster_context->stream_handle,
                              VHD_CORE_SP_LINE_PADDING,
                              128) == VHDERR_INVALIDPROPERTY)
    {
        av_log(videomaster_context->avctx, AV_LOG_INFO,
               "Line padding property not "
               "supported, using YUV422 "
               "8bits as "
               "input video format\n");
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_CORE_SP_BUFFER_PACKING,
                                  VHD_BUFPACK_VIDEO_YUV422_8),
            "", "");
        videomaster_context->video_codec = AV_CODEC_ID_RAWVIDEO;
        videomaster_context->video_pixel_format = AV_PIX_FMT_UYVY422;
        videomaster_context->video_bit_rate =
            av_rescale(videomaster_context->video_width *
                           videomaster_context->video_height * 16,
                       videomaster_context->video_frame_rate_num,
                       videomaster_context->video_frame_rate_den);
    }
    else
    {

        av_log(videomaster_context->avctx, AV_LOG_INFO,
               "Line padding property "
               "supported, using YUV422 "
               "10bits as input "
               "video format\n");
        handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_CORE_SP_BUFFER_PACKING,
                                  VHD_BUFPACK_VIDEO_YUV422_10),
            "", "");
        videomaster_context->video_codec = AV_CODEC_ID_V210;
        videomaster_context->video_bit_rate =
            av_rescale(videomaster_context->video_width *
                           videomaster_context->video_height * 64,
                       videomaster_context->video_frame_rate_num,
                       3 * videomaster_context->video_frame_rate_den);
    }

    // add more delay to get stream and no
    // time-out error
    GET_AND_CHECK(handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_SetStreamProperty(videomaster_context->stream_handle,
                                        VHD_CORE_SP_IO_TIMEOUT, 10000),
                  "Stream time-out has been set to "
                  "10000ms",
                  "Unable to set stream time-out");
    if (videomaster_context->timestamp_source <
        AV_VIDEOMASTER_TIMESTAMP_HARDWARE)
        GET_AND_CHECK(
            handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_SetBoardProperty(
                videomaster_context->board_handle,
                VHD_CORE_BP_SYSTEM_TIME_CLK_TYPE,
                get_videomaster_enumeration_value_for_timestamp_source(
                    videomaster_context->timestamp_source)),
            "System time clock type set "
            "successfully",
            "Failed to set system time clock "
            "type");
    else if (videomaster_context->timestamp_source ==
             AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD)
    {
        GET_AND_CHECK(handle_vhd_status, videomaster_context->avctx,
                      videomaster_context->avctx,
                      VHD_SetBoardProperty(
                          videomaster_context->board_handle,
                          VHD_SDI_BP_BLACKBURST0_DETECTION_ENABLE, FALSE),
                      "Disable Blackburst detection for LTC on-board signal",
                      "Failed to disable Blackburst detection for LTC on-board "
                      "signal");
    }

    GET_AND_CHECK(handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_StartStream(videomaster_context->stream_handle),
                  "Stream started successfully", "Failed to start stream");

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "ff_videomaster_start_stream: IN\n");
    return 0;
}

int ff_videomaster_stop_stream(VideoMasterContext *videomaster_context)
{

    release_audio_info(videomaster_context,
                       &videomaster_context->audio_info.sdi.audio_info);
    return handle_vhd_status(videomaster_context->avctx,
                             VHD_StopStream(videomaster_context->stream_handle),
                             "Stream stopped successfully",
                             "Failed to stop stream");
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
    default:
        return "unknown";
    }
}
