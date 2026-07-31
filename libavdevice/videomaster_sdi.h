/**
 * @file videomaster_sdi.h
 * @brief SDI/ASI-specific declarations for VideoMaster capture.
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

#ifndef AVDEVICE_VIDEOMASTER_SDI_H
#define AVDEVICE_VIDEOMASTER_SDI_H

#include "videomaster_common.h"

/**
 * @brief Validates command-line arguments for SDI/ASI channels.
 *
 * Checks that SDI-specific constraints are met:
 * - No IP-specific arguments are provided.
 * - Audio properties may be specified (warnings if missing).
 *
 * @param videomaster_data The command-line data.
 * @param videomaster_context The VideoMaster context (for logging).
 * @return 0 on success, AVERROR(EINVAL) if invalid combination.
 */
int ff_videomaster_validate_arguments_sdi(
    VideoMasterData *videomaster_data, VideoMasterContext *videomaster_context);

/**
 * @brief Validates audio properties for SDI/ASI channels.
 *
 * Checks that required audio properties are specified (for SDI, audio must be
 * configured).
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success, no-op warning if incomplete.
 */
int ff_videomaster_check_audio_properties_sdi(
    VideoMasterContext *videomaster_context);

/**
 * @brief Validates and retrieves channel properties for SDI/ASI channels.
 *
 * Retrieves video and audio properties, logs SDI-specific details,
 * and opens the stream handle. Sets has_video and has_audio flags.
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success, AVERROR(EIO) on failure.
 */
int ff_videomaster_check_channel_integrity_sdi(
    VideoMasterContext *videomaster_context);

/**
 * @brief Returns whether an SDI/ASI channel is locked.
 *
 * @param videomaster_context The VideoMaster context.
 * @return true if locked, false otherwise.
 */
bool ff_videomaster_is_channel_locked_sdi(
    VideoMasterContext *videomaster_context);

/**
 * @brief Initializes the audio info structure for SDI streams.
 *
 * Called from ff_videomaster_start_stream_sdi() and exported for
 * direct use by videomaster_common.c (HDMI audio property detection).
 *
 * @param videomaster_context The VideoMaster context.
 * @param audio_info The audio info structure to initialize.
 * @return 0 on success, AVERROR(ENOMEM) on allocation failure.
 */
int ff_videomaster_sdi_init_audio_info(VideoMasterContext *videomaster_context,
                                       VHD_AUDIOINFO      *audio_info);

/**
 * @brief Releases SDI/ASI allocated audio channel buffers.
 *
 * Frees audio_info->pAudioGroups[*].pAudioChannels[*].pData where allocated.
 *
 * @param videomaster_context The VideoMaster context.
 * @param audio_info The audio info structure to release.
 * @return 0 on success.
 */
int ff_videomaster_release_audio_info_sdi(
    VideoMasterContext *videomaster_context, VHD_AUDIOINFO *audio_info);

/**
 * @brief Extracts SDI/ASI audio payload and allocates interleaved buffer.
 *
 * Reads SDI audio from slot into audio_info and converts it to an interleaved
 * linear buffer stored in videomaster_context->audio_buffer.
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_get_audio_buffer_sdi(VideoMasterContext *videomaster_context);

/**
 * @brief Returns SDI stream processing mode used when opening streams.
 */
uint32_t ff_videomaster_get_stream_proc_sdi(void);

/**
 * @brief Returns SDI video buffer type for slot extraction.
 */
uint32_t ff_videomaster_get_video_buffer_type_sdi(void);

/**
 * @brief Retrieves SDI/ASI video properties from channel/stream properties.
 *
 * @param avctx AVFormatContext for logging.
 * @param board_handle Board handle.
 * @param stream_handle Stream handle (optional).
 * @param channel_index Channel index.
 * @param video_info Returned SDI video info.
 * @param width Returned active width.
 * @param height Returned active height.
 * @param frame_rate_num Returned frame-rate numerator.
 * @param frame_rate_den Returned frame-rate denominator.
 * @param interlaced Returned interlaced flag.
 * @param dual_stream Whether dual-stream probing is allowed.
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_get_video_stream_properties_sdi(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, union VideoMasterVideoInfo *video_info,
    uint32_t *width, uint32_t *height, uint32_t *frame_rate_num,
    uint32_t *frame_rate_den, bool *interlaced, bool dual_stream);

/**
 * @brief Fills buf with an enriched description for a locked SDI/ASI channel.
 *
 * Uses properties already stored in videomaster_context (resolution,
 * frame-rate, interface).
 *
 * @param videomaster_context The VideoMaster context.
 * @param board_name Name of the board.
 * @param serial_number Serial number of the board.
 * @param buf Caller-allocated buffer to write into.
 * @param buf_size Size of buf in bytes.
 */
void ff_videomaster_format_channel_description_sdi(
    VideoMasterContext *videomaster_context, const char *board_name,
    const char *serial_number, char *buf, size_t buf_size);

/**
 * @brief Configures SDI/ASI-specific stream properties and initializes audio.
 *
 * For ASI-capable channels, sets the channel mode to SDI. Sets video standard,
 * interface, and genlock clock divisor. Initializes audio info if audio is
 * present. Does NOT call VHD_StartStream.
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_start_stream_sdi(VideoMasterContext *videomaster_context);

#endif /* AVDEVICE_VIDEOMASTER_SDI_H */
