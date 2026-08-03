/**
 * @file videomaster_ip.h
 * @brief Internal declarations for IP/ST2110 VideoMaster capture.
 *
 * This header is NOT part of the public API. It exposes IP-specific
 * functions needed by videomaster_common.c.
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

#ifndef AVDEVICE_VIDEOMASTER_IP_H
#define AVDEVICE_VIDEOMASTER_IP_H

#include "videomaster_common.h"

/**
 * @brief Validates command-line arguments for IP/ST2110 channels.
 *
 * Checks that IP-specific constraints are met:
 * - IP-specific arguments MUST be provided for explicit mode.
 * - buffer_packing and dual_stream are not applicable (IP uses fixed values).
 * - Audio arguments are ignored.
 *
 * @param videomaster_data The command-line data.
 * @param videomaster_context The VideoMaster context (for logging).
 * @return 0 on success, AVERROR(EINVAL) if invalid combination.
 */
int ff_videomaster_validate_arguments_ip(
    VideoMasterData *videomaster_data, VideoMasterContext *videomaster_context);

/**
 * @brief Validates audio properties for IP/ST2110 channels.
 *
 * Confirms that audio options are ignored in IP mode (not applicable).
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success.
 */
int ff_videomaster_check_audio_properties_ip(
    VideoMasterContext *videomaster_context);

/**
 * @brief Validates and retrieves channel properties for IP/ST2110 channels.
 *
 * Validates explicit mode configuration, prepares multicast,
 * logs IP-specific stream details, and opens the stream handle.
 * Sets has_video flag.
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success, AVERROR(EINVAL) if not in explicit mode,
 *         AVERROR(EIO) on I/O failure.
 */
int ff_videomaster_check_channel_integrity_ip(
    VideoMasterContext *videomaster_context);

/**
 * @brief Returns whether an IP ST2110 channel is available for listing.
 *
 * Unlike SDI/HDMI, IP channels have no passive hardware lock indicator:
 * VHD_ST2110_RXSTS_VIDEO_UNLOCKED is only cleared after VHD_StartStream()
 * with a fully configured stream and active network traffic.  For the
 * purpose of -sources enumeration the channel is always considered
 * available as long as the board is present.
 *
 * @param videomaster_context The VideoMaster context.
 * @return true always (channel exists on the board).
 */
bool ff_videomaster_is_channel_locked_ip(
    VideoMasterContext *videomaster_context);

/**
 * @brief Returns a human-readable status string for an IP ST2110 channel.
 *
 * Uses VHD_CORE_BP_CHN_AVAILABILITY to determine whether another application
 * already holds a stream handle on this channel.
 *
 * Layout of the availability bitmask (from the SDK header):
 *   bits  0- 3: RX0-RX3    bits  4- 7: TX0-TX3
 *   bits  8-11: RX4-RX7    bits 12-15: TX4-TX7
 *   bits 16-19: RX8-RX11   bits 20-23: TX8-TX11
 * Formula for RX channel N: bit = (N / 4) * 8 + (N % 4)
 * A bit set to 1 means the channel is free.
 *
 * @param videomaster_context The VideoMaster context.
 * @return "available" if free, "in use" if already locked by another app.
 */
const char *
ff_videomaster_get_channel_status_ip(VideoMasterContext *videomaster_context);

/**
 * @brief Returns IP/ST2110 video buffer type for slot extraction.
 */
uint32_t ff_videomaster_get_video_buffer_type_ip();

/**
 * @brief Prepares the IP board for main stream reception.
 *
 * For IP ST2110 channels: joins the multicast group on the main ethernet
 * port (port 0) if ip_destination is a multicast address.
 * Must be called after ff_videomaster_open_board_handle() and before
 * ff_videomaster_open_stream_handle() for IP channels.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure.
 */
int ff_videomaster_join_multicast_group(
    VideoMasterContext *videomaster_context);

/**
 * @brief Prepares the IP board for main stream reception.
 *
 * For IP ST2110 channels: leaves the multicast group on the main ethernet
 * port (port 0) if ip_destination is a multicast address.
 *
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure.
 */
int ff_videomaster_leave_multicast_group(
    VideoMasterContext *videomaster_context);

/**
 * @brief Configures all ST2110-20 stream properties for explicit mode.
 *
 * Sets video standard, sampling, depth, SPS disabled, destination IP,
 * optional source IP (unicast RX filtering), optional UDP destination and
 * source ports, optional RTP payload type filtering, and buffer packing
 * determined by bit depth. Also sets video_codec, video_pixel_format and
 * video_bit_rate on the context. Does NOT call VHD_StartStream.
 *
 * For the SPS stream (when ip_sps_destination is set), also configures
 * optional SPS source IP and SPS UDP source port, and sets the SPS RX
 * filtering mask (VHD_ST2110_SP_SPS_FILTERING_MASK).
 *
 * Called from ff_videomaster_start_stream() in videomaster_common.c.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure.
 */
int ff_videomaster_start_stream_ip_explicit(
    VideoMasterContext *videomaster_context);

/**
 * @brief Configures IP/ST2110-specific stream properties.
 *
 * Wrapper that dispatches to IP-specific setup based on mode.
 * For explicit mode: calls ff_videomaster_start_stream_ip_explicit().
 * For SDP mode: calls ff_videomaster_start_stream_ip_sdp().
 * For auto mode: performs minimal or no additional setup.
 *
 * @param videomaster_context The VideoMaster context.
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_start_stream_ip(VideoMasterContext *videomaster_context);

/**
 * @brief Parses an SDP file and populates the IP/ST2110 context fields.
 *
 * Reads the file at videomaster_data->ip_sdp_file, parses it with
 * VHD_ReadSDP(), and extracts stream parameters (destination IPs, ports,
 * payload types, video standard and characteristics) into videomaster_context.
 * Sets videomaster_context->ip_sdp_mode to true on success.
 *
 * Errors:
 * - AVERROR(ENOENT)  if the file does not exist.
 * - AVERROR(EINVAL)  if the SDP is non-compliant or contains unsupported
 *                    content (>2 media entries, non-ST2110-20 type, IPv6).
 *
 * @param videomaster_data    Raw command-line data (provides the file path).
 * @param videomaster_context Context to populate.
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_parse_sdp_file(VideoMasterData    *videomaster_data,
                                  VideoMasterContext *videomaster_context);

/**
 * @brief Configures all ST2110-20 stream properties from a parsed SDP.
 *
 * Applies video standard, sampling, depth, destination/source IP, UDP
 * port, RTP payload type, and buffer packing from the SDP media entries
 * stored in videomaster_context. Enables the SPS sub-stream when a second
 * media entry is present. Also sets video_codec, video_pixel_format, and
 * video_bit_rate. Does NOT call VHD_StartStream.
 *
 * @param videomaster_context The VideoMaster context (ip_sdp_mode must be
 *                            true).
 * @return 0 on success, negative AVERROR code on failure.
 */
int ff_videomaster_start_stream_ip_sdp(
    VideoMasterContext *videomaster_context);

#endif /* AVDEVICE_VIDEOMASTER_IP_H */
