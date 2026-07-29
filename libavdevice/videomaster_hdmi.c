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
    /* IP arguments must not be specified for HDMI */
    if (videomaster_data->ip_destination != NULL ||
        videomaster_data->ip_udp_port > 0 ||
        videomaster_data->ip_payload_type > 0 ||
        videomaster_data->ip_video_width > 0 ||
        videomaster_data->ip_video_height > 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "IP-specific arguments (ip_destination, ip_udp_port, "
               "ip_payload_type, ip_video_*) are not applicable for HDMI "
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
             videomaster_context->audio_sample_size, board_name,
             serial_number);
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

