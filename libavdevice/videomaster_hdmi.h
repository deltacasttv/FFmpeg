/**
 * @file videomaster_hdmi.h
 * @brief HDMI/DV-specific declarations for VideoMaster capture.
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

#ifndef AVDEVICE_VIDEOMASTER_HDMI_H
#define AVDEVICE_VIDEOMASTER_HDMI_H

#include "videomaster_common.h"

/* ---- Demuxer validation ---- */

/**
 * @brief Validates command-line arguments for HDMI/DV channels.
 *
 * Checks that HDMI-specific constraints are met:
 * - No IP-specific arguments are provided.
 *
 * @param videomaster_data The command-line data.
 * @param videomaster_context The VideoMaster context (for logging).
 * @return 0 on success, AVERROR(EINVAL) if invalid combination.
 */
int ff_videomaster_validate_arguments_hdmi(
    VideoMasterData *videomaster_data, VideoMasterContext *videomaster_context);

/**
 * @brief Validates audio properties for HDMI/DV channels.
 *
 * Checks that audio properties are not manually specified (they're
 * auto-detected).
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success.
 */
int ff_videomaster_check_audio_properties_hdmi(
    VideoMasterContext *videomaster_context);

/**
 * @brief Validates and retrieves channel properties for HDMI/DV channels.
 *
 * Retrieves video and audio properties, logs HDMI-specific details,
 * and opens the stream handle. Sets has_video and has_audio flags.
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success, AVERROR(EIO) on failure.
 */
int ff_videomaster_check_channel_integrity_hdmi(
    VideoMasterContext *videomaster_context);

/**
 * @brief Returns whether an HDMI channel is locked.
 *
 * @param videomaster_context The VideoMaster context.
 * @return true if locked, false otherwise.
 */
bool ff_videomaster_is_channel_locked_hdmi(
    VideoMasterContext *videomaster_context);

/**
 * @brief Fills buf with an enriched description for a locked HDMI channel.
 *
 * Uses properties already stored in videomaster_context (resolution,
 * frame-rate, color space, cable bit sampling, audio params).
 *
 * @param videomaster_context The VideoMaster context.
 * @param board_name Name of the board.
 * @param serial_number Serial number of the board.
 * @param buf Caller-allocated buffer to write into.
 * @param buf_size Size of buf in bytes.
 */
void ff_videomaster_format_channel_description_hdmi(
    VideoMasterContext *videomaster_context, const char *board_name,
    const char *serial_number, char *buf, size_t buf_size);

/**
 * @brief Maps HDMI cable bit sampling to default logical buffer packing.
 *
 * @param cable_bit_sampling HDMI cable bit sampling value.
 * @return Matching AVVideoMasterBufferPacking.
 */
enum AVVideoMasterBufferPacking
ff_videomaster_get_buffer_packing_from_cable_bit_sampling_hdmi(
    VHD_DV_SAMPLING cable_bit_sampling);

/**
 * @brief Retrieves HDMI audio properties from DV audio infoframe/AES status.
 *
 * @param avctx AVFormatContext for logging.
 * @param board_handle Board handle.
 * @param stream_handle Stream handle.
 * @param channel_index Channel index.
 * @param buffer_packing Selected buffer packing.
 * @param audio_info Returned HDMI audio info.
 * @param sample_rate Returned sample rate.
 * @param nb_channels Returned number of channels.
 * @param sample_size Returned sample size.
 * @param codec Returned codec.
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_get_audio_stream_properties_from_audio_infoframe_hdmi(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterBufferPacking buffer_packing,
    union VideoMasterAudioInfo *audio_info, uint32_t *sample_rate,
    uint32_t *nb_channels, uint32_t *sample_size, enum AVCodecID *codec);

/**
 * @brief Extracts HDMI audio payload from current slot into context buffer.
 *
 * @param videomaster_context VideoMaster context.
 * @param channel_mask Audio channel mask to extract.
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_get_audio_buffer_hdmi(
    VideoMasterContext *videomaster_context, int channel_mask);

/**
 * @brief Returns HDMI stream processing mode used when opening streams.
 */
uint32_t ff_videomaster_get_stream_proc_hdmi(void);

/**
 * @brief Returns HDMI video buffer type for slot extraction.
 */
uint32_t ff_videomaster_get_video_buffer_type_hdmi(void);

/**
 * @brief Retrieves HDMI video properties from channel/stream properties.
 *
 * @param avctx AVFormatContext for logging.
 * @param board_handle Board handle.
 * @param stream_handle Stream handle (optional).
 * @param channel_index Channel index.
 * @param video_info Returned HDMI video info.
 * @param width Returned active width.
 * @param height Returned active height.
 * @param frame_rate_num Returned frame-rate numerator.
 * @param frame_rate_den Returned frame-rate denominator.
 * @param interlaced Returned interlaced flag.
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_get_video_stream_properties_hdmi(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, union VideoMasterVideoInfo *video_info,
    uint32_t *width, uint32_t *height, uint32_t *frame_rate_num,
    uint32_t *frame_rate_den, bool *interlaced);

/* ---- Stream setup ---- */

/**
 * @brief Configures HDMI/DV-specific stream properties.
 *
 * Sets active width, height, refresh rate, pixel clock, color space,
 * and cable bit sampling. Does NOT call VHD_StartStream.
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_start_stream_hdmi(VideoMasterContext *videomaster_context);

#endif /* AVDEVICE_VIDEOMASTER_HDMI_H */
