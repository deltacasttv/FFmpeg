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
