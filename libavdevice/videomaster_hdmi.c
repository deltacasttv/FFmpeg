/**
 * @file videomaster_hdmi.c
 * @brief HDMI/DV-specific implementation for the VideoMaster DELTACAST(c)
 *        input device.
 *
 * This file contains both stream setup and demuxer enumeration/validation
 * for HDMI channels. It is intentionally isolated from SDI and IP pipelines.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "videomaster_hdmi.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "videomaster_internal.h"

#if defined(__APPLE__)
#include <VideoMasterHD/VideoMasterHD_Core.h>
#include <VideoMasterHD/VideoMasterHD_String.h>
#else
#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_String.h>
#endif

/* Forward declared from videomaster_common.c */
extern int
ff_videomaster_open_stream_handle(VideoMasterContext *videomaster_context);
extern int ff_videomaster_get_video_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterChannelType *channel_type,
    union VideoMasterVideoInfo *video_info, uint32_t *video_width,
    uint32_t *video_height, uint32_t *video_frame_rate_num,
    uint32_t *video_frame_rate_den, bool *video_interlaced, bool dual_stream);
extern int ff_videomaster_get_audio_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterBufferPacking buffer_packing,
    enum AVVideoMasterChannelType *channel_type,
    union VideoMasterAudioInfo *audio_info, uint32_t *sample_rate,
    uint32_t *nb_channels, uint32_t *sample_size, enum AVCodecID *codec);

/* ---- Demuxer validation functions ---- */

int ff_videomaster_validate_arguments_hdmi(
    VideoMasterData *videomaster_data, VideoMasterContext *videomaster_context)
{
    /* dual_stream is SDI-only */
    if (videomaster_context->dual_stream)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "dual_stream is not applicable for HDMI channels.\n");
        return AVERROR(EINVAL);
    }

    /* IP arguments must not be specified for HDMI */
    if (videomaster_data->ip_video_destination != NULL ||
        videomaster_data->ip_video_sps_destination != NULL ||
        videomaster_data->ip_video_udp_port > 0 ||
        videomaster_data->ip_video_sps_udp_port > 0 ||
        videomaster_data->ip_video_payload_type > 0 ||
        videomaster_data->ip_video_sps_payload_type > 0 ||
        videomaster_data->ip_video_width > 0 ||
        videomaster_data->ip_video_height > 0)
    {
        av_log(
            videomaster_context->avctx, AV_LOG_ERROR,
            "IP-specific arguments (ip_video_destination, ip_video_udp_port, "
            "ip_video_payload_type, ip_video_*) are not applicable for HDMI "
            "channels.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

int ff_videomaster_check_audio_properties_hdmi(
    VideoMasterContext *videomaster_context)
{
    if (videomaster_context->audio_nb_channels != -1 ||
        videomaster_context->audio_sample_rate != -1 ||
        videomaster_context->audio_sample_size != -1)
    {
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Audio properties are not applicable for HDMI channels. These "
               "value will be overridden with auto-detection.\n");
    }
    return 0;
}

int ff_videomaster_check_channel_integrity_hdmi(
    VideoMasterContext *videomaster_context)
{
    float frame_rate = (float)videomaster_context->video_frame_rate_num /
                       videomaster_context->video_frame_rate_den;

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Channel index is valid\n");

    if (ff_videomaster_get_video_stream_properties(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->stream_handle,
            videomaster_context->channel_index,
            &videomaster_context->channel_type,
            &videomaster_context->video_info, &videomaster_context->video_width,
            &videomaster_context->video_height,
            &videomaster_context->video_frame_rate_num,
            &videomaster_context->video_frame_rate_den,
            &videomaster_context->video_interlaced,
            videomaster_context->dual_stream) == 0)
    {
        videomaster_context->has_video = true;

        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Stream properties: %ux%u@%.3f %s %s\n",
               videomaster_context->video_width,
               videomaster_context->video_height, frame_rate,
               VHD_DV_CS_ToPrettyString(
                   videomaster_context->video_info.hdmi.color_space),
               VHD_DV_SAMPLING_ToPrettyString(
                   videomaster_context->video_info.hdmi.cable_bit_sampling));
        av_log(videomaster_context->avctx, AV_LOG_TRACE, "Pixel clock: %u\n",
               videomaster_context->video_info.hdmi.pixel_clock);
        av_log(videomaster_context->avctx, AV_LOG_TRACE, "Interlaced: %s\n",
               videomaster_context->video_interlaced ? "true" : "false");
        av_log(videomaster_context->avctx, AV_LOG_TRACE, "Color space: %s\n",
               VHD_DV_CS_ToPrettyString(
                   videomaster_context->video_info.hdmi.color_space));
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Cable bit sampling: %s\n",
               VHD_DV_SAMPLING_ToPrettyString(
                   videomaster_context->video_info.hdmi.cable_bit_sampling));
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Selected Buffer Packing: %s\n",
               VHD_BUFFERPACKING_ToPrettyString(
                   videomaster_context->video_buffer_packing));

        if (ff_videomaster_open_stream_handle(videomaster_context) != 0)
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Failed to open stream handle.\n");
            return AVERROR(EIO);
        }
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Stream handle opened successfully\n");
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to get stream properties\n");
        return AVERROR(EIO);
    }

    if (ff_videomaster_get_audio_stream_properties(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->stream_handle,
            videomaster_context->channel_index,
            videomaster_context->video_buffer_packing,
            &videomaster_context->channel_type,
            &videomaster_context->audio_info,
            &videomaster_context->audio_sample_rate,
            &videomaster_context->audio_nb_channels,
            &videomaster_context->audio_sample_size,
            &videomaster_context->audio_codec) == 0)
    {
        if (videomaster_context->audio_sample_size != 0 &&
            videomaster_context->audio_nb_channels != 0)
        {
            videomaster_context->has_audio = true;
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "Audio properties: %u channels @%uHz (%u bits)\n",
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
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Failed to get audio properties\n");
    }

    return 0;
}

bool ff_videomaster_is_channel_locked_hdmi(
    VideoMasterContext *videomaster_context)
{
    uint32_t channel_status = 0;

    if (ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_GetChannelProperty(videomaster_context->board_handle,
                                   VHD_RX_CHANNEL,
                                   videomaster_context->channel_index,
                                   VHD_CORE_CP_STATUS, &channel_status),
            "Channel status retrieved successfully",
            "Failed to retrieve channel status") != 0)
        return false;

    return !(channel_status & VHD_CORE_RXSTS_UNLOCKED);
}

void ff_videomaster_format_channel_description_hdmi(
    VideoMasterContext *videomaster_context, const char *board_name,
    const char *serial_number, char *buf, size_t buf_size)
{
    double frame_rate = (double)videomaster_context->video_frame_rate_num /
                        videomaster_context->video_frame_rate_den;

    snprintf(buf, buf_size,
             "HDMI video: %ux%u%s%.3f %s %s, audio: %u channels @%uHz "
             "(%u bits) on board %s (SN: %s)",
             videomaster_context->video_width,
             videomaster_context->video_height,
             videomaster_context->video_interlaced ? "i" : "p", frame_rate,
             VHD_DV_CS_ToPrettyString(
                 videomaster_context->video_info.hdmi.color_space),
             VHD_DV_SAMPLING_ToPrettyString(
                 videomaster_context->video_info.hdmi.cable_bit_sampling),
             videomaster_context->audio_nb_channels,
             videomaster_context->audio_sample_rate,
             videomaster_context->audio_sample_size, board_name, serial_number);
}

enum AVVideoMasterBufferPacking
ff_videomaster_get_buffer_packing_from_cable_bit_sampling_hdmi(
    VHD_DV_SAMPLING cable_bit_sampling)
{
    switch (cable_bit_sampling)
    {
    case VHD_DV_SAMPLING_4_2_0_8BITS:
        return AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_NV12;
    case VHD_DV_SAMPLING_4_2_0_10BITS:
        return AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_P010;
    default:
        return AV_VIDEOMASTER_BUFFER_PACKING_YUV422_10;
    }
}

static int lock_slot_hdmi(VideoMasterContext *videomaster_context)
{
    return ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_LockSlotHandle(videomaster_context->stream_handle,
                           &videomaster_context->slot_handle),
        "Slot handle locked successfully", "Failed to lock slot handle");
}

static int unlock_slot_hdmi(VideoMasterContext *videomaster_context)
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

static int get_sample_size_from_audio_infoframe_and_aes_status_hdmi(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    uint32_t *sample_size)
{
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

static int get_sample_rate_from_audio_infoframe_and_aes_status_hdmi(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    uint32_t *sample_rate)
{
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

static int get_nb_channels_from_audio_infoframe_and_aes_status_hdmi(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    uint32_t *nb_channels)
{
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

static int get_codec_from_audio_infoframe_and_aes_status_hdmi(
    VideoMasterContext    *videomaster_context,
    VHD_DV_AUDIO_INFOFRAME audio_info_frame, VHD_DV_AUDIO_AES_STS aes_status,
    enum AVCodecID *codec_id)
{
    int      return_code = 0;
    uint32_t sample_size = 0;

    switch (audio_info_frame.CodingType)
    {
    case VHD_DV_AUDIO_INFOFRAME_CODING_TYPE_PCM:
        get_sample_size_from_audio_infoframe_and_aes_status_hdmi(
            videomaster_context, audio_info_frame, aes_status, &sample_size);
        if (sample_size == 16)
            *codec_id = AV_CODEC_ID_PCM_S16LE;
        else if (sample_size == 24)
            *codec_id = AV_CODEC_ID_PCM_S24LE;
        else if (sample_size == 20)
            *codec_id = AV_CODEC_ID_PCM_S24LE;
        else
            return_code = AVERROR(EINVAL);
        break;
    case VHD_DV_AUDIO_INFOFRAME_CODING_TYPE_REF_STREAM_HEADER:
        switch (aes_status.LinearPCM)
        {
        case VHD_DV_AUDIO_AES_SAMPLE_STS_LINEAR_PCM_SAMPLE:
            get_sample_size_from_audio_infoframe_and_aes_status_hdmi(
                videomaster_context, audio_info_frame, aes_status,
                &sample_size);
            if (sample_size == 16)
                *codec_id = AV_CODEC_ID_PCM_S16LE;
            else if (sample_size == 24)
                *codec_id = AV_CODEC_ID_PCM_S24LE;
            else if (sample_size == 20)
                *codec_id = AV_CODEC_ID_PCM_S24LE;
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

int ff_videomaster_get_audio_stream_properties_from_audio_infoframe_hdmi(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterBufferPacking buffer_packing,
    union VideoMasterAudioInfo *audio_info, uint32_t *sample_rate,
    uint32_t *nb_channels, uint32_t *sample_size, enum AVCodecID *codec)
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
        .video_buffer_packing = buffer_packing,
    };

    VideoMasterContext *videomaster_context = &videomaster_context_struct;

    GET_AND_CHECK(ff_videomaster_handle_av_error, videomaster_context->avctx,
                  videomaster_context->avctx,
                  ff_videomaster_get_video_stream_properties(
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
                      &videomaster_context->video_interlaced,
                      videomaster_context->dual_stream),
                  "Video stream properties retrieved successfully",
                  "Could not retrieve video stream properties");

    *audio_info = (union VideoMasterAudioInfo){ 0 };
    *codec = AV_CODEC_ID_NONE;

    GET_AND_CHECK(ff_videomaster_start_stream, avctx, videomaster_context);

    GET_AND_CHECK_AND_STOP_STREAM(lock_slot_hdmi, avctx, videomaster_context);

    GET_AND_CHECK_AND_STOP_STREAM(
        ff_videomaster_handle_vhd_status, avctx, avctx,
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
        ff_videomaster_handle_av_error, avctx, avctx,
        get_sample_size_from_audio_infoframe_and_aes_status_hdmi(
            videomaster_context, audio_info_frame, audio_aes_status,
            sample_size),
        "", "Failed to get audio bits per sample from audio info frame");

    GET_AND_CHECK_AND_STOP_STREAM(
        ff_videomaster_handle_av_error, avctx, avctx,
        get_sample_rate_from_audio_infoframe_and_aes_status_hdmi(
            videomaster_context, audio_info_frame, audio_aes_status,
            sample_rate),
        "", "Failed to get audio sample rate from audio info frame");

    GET_AND_CHECK_AND_STOP_STREAM(
        ff_videomaster_handle_av_error, avctx, avctx,
        get_nb_channels_from_audio_infoframe_and_aes_status_hdmi(
            videomaster_context, audio_info_frame, audio_aes_status,
            nb_channels),
        "", "Failed to get audio channels from audio info frame");

    GET_AND_CHECK_AND_STOP_STREAM(
        ff_videomaster_handle_av_error, avctx, avctx,
        get_codec_from_audio_infoframe_and_aes_status_hdmi(
            videomaster_context, audio_info_frame, audio_aes_status, codec),
        "", "Unsupported non PCM audio format");

    if (*sample_size == 16)
        audio_info->hdmi.format = VHD_DVAUDIOFORMAT_16;
    else if (*sample_size == 24 || *sample_size == 20)
        audio_info->hdmi.format = VHD_DVAUDIOFORMAT_24;

    GET_AND_CHECK_AND_STOP_STREAM(unlock_slot_hdmi, avctx, videomaster_context);

    GET_AND_CHECK(ff_videomaster_stop_stream, avctx, videomaster_context);

    return av_error;
}

int ff_videomaster_get_audio_buffer_hdmi(
    VideoMasterContext *videomaster_context, int channel_mask)
{
    VHD_DV_AUDIO_TYPE      audio_type;
    VHD_DV_AUDIO_INFOFRAME audio_infoframe;
    VHD_DV_AUDIO_AES_STS   audio_aes_status;

    if (ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_GetSlotDvAudioInfo(videomaster_context->slot_handle,
                                   &audio_type, &audio_infoframe,
                                   &audio_aes_status),
            "Audio slot buffer retrieved successfully",
            "Failed to retrieve audio slot buffer") != 0)
        return AVERROR(EIO);

    if (audio_type == VHD_DV_AUDIO_TYPE_NONE)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "No audio type detected in audio InfoFrame or stream "
               "header.\n");
        return AVERROR(EIO);
    }

    videomaster_context->audio_buffer_size = 0;
    if (audio_aes_status.LinearPCM !=
        VHD_DV_AUDIO_AES_SAMPLE_STS_LINEAR_PCM_SAMPLE)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Non PCM audio is not supported\n");
        return AVERROR(EIO);
    }

    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SlotExtractDvPCMAudio(videomaster_context->slot_handle,
                                  videomaster_context->audio_info.hdmi.format,
                                  channel_mask, NULL,
                                  &videomaster_context->audio_buffer_size),
        "", "");
    videomaster_context->audio_buffer = av_mallocz(
        videomaster_context->audio_buffer_size);
    if (ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_SlotExtractDvPCMAudio(
                videomaster_context->slot_handle,
                videomaster_context->audio_info.hdmi.format, channel_mask,
                videomaster_context->audio_buffer,
                &videomaster_context->audio_buffer_size),
            "Audio slot buffer retrieved successfully",
            "Failed to retrieve audio slot buffer") != 0)
    {
        av_freep(&videomaster_context->audio_buffer);
        return AVERROR(EIO);
    }

    return 0;
}

uint32_t ff_videomaster_get_stream_proc_hdmi(void)
{
    return VHD_DV_STPROC_JOINED;
}

uint32_t ff_videomaster_get_video_buffer_type_hdmi(void)
{
    return VHD_DV_BT_VIDEO;
}

int ff_videomaster_get_video_stream_properties_hdmi(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, union VideoMasterVideoInfo *video_info,
    uint32_t *width, uint32_t *height, uint32_t *frame_rate_num,
    uint32_t *frame_rate_den, bool *interlaced)
{
    uint32_t frame_rate = 0;
    uint32_t total_width = 0;
    uint32_t total_height = 0;
    HANDLE   local_stream_handle = stream_handle;
    int      av_status = 0;
    BOOL32   interlaced_tmp = 0;

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                    VHD_DV_CP_ACTIVE_WIDTH, width),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get active width from channel properties for "
               "HDMI channel %u\n",
               channel_index);
        return av_status;
    }

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                    VHD_DV_CP_ACTIVE_HEIGHT, height),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get active height from channel properties for "
               "HDMI channel %u\n",
               channel_index);
        return av_status;
    }

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                    VHD_DV_CP_REFRESH_RATE, &frame_rate),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get refresh rate from channel properties for "
               "HDMI channel %u\n",
               channel_index);
        return av_status;
    }

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                    VHD_DV_CP_PIXEL_CLOCK,
                                    &video_info->hdmi.pixel_clock),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get pixel clock from channel properties for "
               "HDMI channel %u\n",
               channel_index);
        return av_status;
    }

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                    VHD_DV_CP_INTERLACED,
                                    (uint32_t *)&interlaced_tmp),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get interlaced property from channel properties for "
               "HDMI channel %u\n",
               channel_index);
        return av_status;
    }
    *interlaced = !!interlaced_tmp;

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                                    VHD_DV_CP_CABLE_COLOR_SPACE,
                                    (uint32_t *)&video_info->hdmi.color_space),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get cable color space from channel properties for "
               "HDMI channel %u\n",
               channel_index);
        return av_status;
    }

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetChannelProperty(
                 board_handle, VHD_RX_CHANNEL, channel_index,
                 VHD_DV_CP_CABLE_BIT_SAMPLING,
                 (uint32_t *)&video_info->hdmi.cable_bit_sampling),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get cable bit sampling from channel properties for "
               "HDMI channel %u\n",
               channel_index);
        return av_status;
    }

    if (local_stream_handle == NULL)
    {
        if ((av_status = ff_videomaster_handle_vhd_status(
                 avctx,
                 VHD_OpenStreamHandle(
                     board_handle,
                     ff_videomaster_get_rx_stream_type_from_index(
                         channel_index),
                     ff_videomaster_get_stream_proc_hdmi(), NULL,
                     &local_stream_handle, NULL),
                 "Stream handle opened successfully",
                 "Failed to open stream handle")) != 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to open stream handle for HDMI channel %u\n",
                   channel_index);
            return av_status;
        }
    }

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetStreamProperty(local_stream_handle, VHD_DV_SP_TOTAL_WIDTH,
                                   (uint32_t *)&total_width),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get total width from stream properties for "
               "HDMI channel %u\n",
               channel_index);
        if (stream_handle == NULL)
            VHD_CloseStreamHandle(local_stream_handle);
        return av_status;
    }

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetStreamProperty(local_stream_handle, VHD_DV_SP_TOTAL_HEIGHT,
                                   (uint32_t *)&total_height),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get total height from stream properties for "
               "HDMI channel %u\n",
               channel_index);
        if (stream_handle == NULL)
            VHD_CloseStreamHandle(local_stream_handle);
        return av_status;
    }

    if (stream_handle == NULL)
    {
        if ((av_status = ff_videomaster_handle_vhd_status(
                 avctx, VHD_CloseStreamHandle(local_stream_handle),
                 "Stream handle closed successfully",
                 "Failed to close stream handle")) != 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to close stream handle for HDMI channel %u\n",
                   channel_index);
            return av_status;
        }
    }

    video_info->hdmi.refresh_rate = frame_rate;

    uint64_t num_u64 = (uint64_t)video_info->hdmi.pixel_clock * 1000;
    uint64_t den_u64 = (uint64_t)total_width * (uint64_t)total_height;
    uint64_t gcd = av_gcd(num_u64, den_u64);

    if (gcd > 1)
    {
        num_u64 /= gcd;
        den_u64 /= gcd;
    }

    while (num_u64 > UINT32_MAX || den_u64 > UINT32_MAX)
    {
        num_u64 >>= 1;
        den_u64 >>= 1;
    }

    *frame_rate_num = (uint32_t)num_u64;
    *frame_rate_den = (uint32_t)den_u64;

    return 0;
}

/* ---- Stream setup functions ---- */

int ff_videomaster_start_stream_hdmi(VideoMasterContext *videomaster_context)
{
    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(videomaster_context->stream_handle,
                              VHD_DV_SP_ACTIVE_WIDTH,
                              videomaster_context->video_width),
        "", "");
    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(videomaster_context->stream_handle,
                              VHD_DV_SP_ACTIVE_HEIGHT,
                              videomaster_context->video_height),
        "", "");
    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(videomaster_context->stream_handle,
                              VHD_DV_SP_INTERLACED,
                              (ULONG)videomaster_context->video_interlaced),
        "", "");
    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(
            videomaster_context->stream_handle, VHD_DV_SP_REFRESH_RATE,
            videomaster_context->video_info.hdmi.refresh_rate),
        "", "");
    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(videomaster_context->stream_handle,
                              VHD_DV_SP_PIXEL_CLOCK,
                              videomaster_context->video_info.hdmi.pixel_clock),
        "", "");
    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(
            videomaster_context->stream_handle, VHD_DV_SP_CS,
            (ULONG)videomaster_context->video_info.hdmi.color_space),
        "", "");
    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(
            videomaster_context->stream_handle, VHD_DV_SP_CABLE_BIT_SAMPLING,
            (ULONG)videomaster_context->video_info.hdmi.cable_bit_sampling),
        "", "");

    return 0;
}
