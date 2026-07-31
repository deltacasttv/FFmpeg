/**
 * @file videomaster_ip.c
 * @brief IP/ST2110 specific implementation for the VideoMaster DELTACAST(c)
 *        input device.
 *
 * This file contains both stream setup and demuxer enumeration for ST2110-20
 * stream configuration and multicast management.
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

#include "videomaster_ip.h"
#include "videomaster_internal.h"

#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"

#if defined(__APPLE__)
#include <VideoMasterHD/VideoMasterHD_Core.h>
#include <VideoMasterHD/VideoMasterHD_Ip_Board.h>
#include <VideoMasterHD/VideoMasterHD_Ip_ST2110_20.h>
#include <VideoMasterHD/VideoMasterHD_Ip_ST2110_Board.h>
#else
#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_Ip_Board.h>
#include <VideoMasterHD_Ip_ST2110_20.h>
#include <VideoMasterHD_Ip_ST2110_Board.h>
#endif

/* ---- Private helpers ---- */

/**
 * @brief Returns true when addr falls within the IPv4 multicast range
 *        (224.0.0.0/4 — first octet in [224, 239]).
 *
 * @param ipv4_addr Host-byte-order 32-bit IPv4 address.
 */
static bool ip_is_multicast(uint32_t ipv4_addr)
{
    return (ipv4_addr >> 24) >= 224 && (ipv4_addr >> 24) <= 239;
}

/**
 * @brief   Validates the arguments for an IP/ST2110 stream (video side).
 * Returns AVERROR(EINVAL) if any argument is invalid.
 *
 * @param videomaster_data    IP/ST2110 stream data structure.
 * @param videomaster_context IP/ST2110 stream context.
 * @return int               0 if arguments are valid, AVERROR(EINVAL)
 * otherwise.
 */
static int validate_video_arguments(VideoMasterData    *videomaster_data,
                                    VideoMasterContext *videomaster_context)
{
    CHECK_INT64_ARG_HAS_BEEN_SET(videomaster_context->avctx,
                                 videomaster_data->ip_video_width,
                                 "ip_video_width",
                                 ff_videomaster_channel_type_to_string(
                                     videomaster_context->channel_type));
    CHECK_INT64_ARG_HAS_BEEN_SET(videomaster_context->avctx,
                                 videomaster_data->ip_video_height,
                                 "ip_video_height",
                                 ff_videomaster_channel_type_to_string(
                                     videomaster_context->channel_type));
    CHECK_INT64_ARG_HAS_BEEN_SET(videomaster_context->avctx,
                                 videomaster_data->ip_video_framerate_num,
                                 "ip_video_framerate_num",
                                 ff_videomaster_channel_type_to_string(
                                     videomaster_context->channel_type));
    CHECK_INT64_ARG_HAS_BEEN_SET(videomaster_context->avctx,
                                 videomaster_data->ip_video_framerate_den,
                                 "ip_video_framerate_den",
                                 ff_videomaster_channel_type_to_string(
                                     videomaster_context->channel_type));
    CHECK_INT64_ARG_HAS_BEEN_SET(videomaster_context->avctx,
                                 videomaster_data->ip_video_interlaced,
                                 "ip_video_interlaced",
                                 ff_videomaster_channel_type_to_string(
                                     videomaster_context->channel_type));
    CHECK_INT64_ARG_HAS_BEEN_SET(videomaster_context->avctx,
                                 videomaster_data->ip_video_bit_depth,
                                 "ip_video_bit_depth",
                                 ff_videomaster_channel_type_to_string(
                                     videomaster_context->channel_type));

    return 0;
}

/**
 * @brief   Validates the arguments for an IP/ST2110 stream (network side).
 * Returns AVERROR(EINVAL) if any argument is invalid.
 *
 * @param videomaster_data    IP/ST2110 stream data structure.
 * @param videomaster_context IP/ST2110 stream context.
 * @return int               0 if arguments are valid, AVERROR(EINVAL)
 * otherwise.
 */
static int validate_network_arguments(VideoMasterData    *videomaster_data,
                                      VideoMasterContext *videomaster_context)
{
    if (videomaster_data->ip_destination == NULL)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Argument ip_destination is required for %s channels.\n",
               ff_videomaster_channel_type_to_string(
                   videomaster_context->channel_type));
        return AVERROR(EINVAL);
    }

    CHECK_INT64_ARG_HAS_BEEN_SET(videomaster_context->avctx,
                                 videomaster_data->ip_udp_port, "ip_udp_port",
                                 ff_videomaster_channel_type_to_string(
                                     videomaster_context->channel_type));

    return 0;
}

/**
 * @brief Searches the ST2110-20 video standard table for an entry that
 *        matches the explicit resolution, framerate and interlacing stored
 *        in the context.
 *
 * The match is done by calling VHD_ST2110_20_GetVideoCharacteristics for
 * every known standard and comparing with the user-supplied parameters.
 * The framerate comparison is intentionally integer (floor), which allows
 * a fractional 59.94 standard to match a "60000/1001" user specification.
 *
 * @param videomaster_context Context carrying the explicit video
 * parameters.
 * @param video_standard      Output: matched ST2110-20 video standard.
 * @return 0 on success, AVERROR(EINVAL) if no standard matches.
 */
static int get_st2110_video_standard_from_explicit(
    VideoMasterContext           *videomaster_context,
    VHD_ST2110_20_VIDEO_STANDARD *video_standard)
{
    for (int i = 0; i < NB_VHD_ST2110_20_VIDEO_STANDARD; i++)
    {
        ULONG  width = 0;
        ULONG  height = 0;
        BOOL32 interlaced = FALSE;
        ULONG  frame_rate = 0;
        BOOL32 is_1001 = FALSE;
        ULONG  status = VHD_ST2110_20_GetVideoCharacteristics(
            (VHD_ST2110_20_VIDEO_STANDARD)i, &width, &height, &interlaced,
            &frame_rate, &is_1001);

        if (status != VHDERR_NOERROR)
            continue;

        if (width != videomaster_context->video_width ||
            height != videomaster_context->video_height ||
            !!interlaced != videomaster_context->video_interlaced)
            continue;

        if ((uint32_t)frame_rate * 1000 ==
            videomaster_context->video_frame_rate_num)
        {
            uint32_t expected_den = is_1001 ? 1001 : 1000;
            if (videomaster_context->video_frame_rate_den == expected_den)
            {
                *video_standard = (VHD_ST2110_20_VIDEO_STANDARD)i;
                return 0;
            }
        }
    }

    return AVERROR(EINVAL);
}

/* ---- Demuxer validation functions ---- */

int ff_videomaster_validate_arguments_ip(
    VideoMasterData *videomaster_data, VideoMasterContext *videomaster_context)
{
    /* buffer_packing is determined by the stream bit depth for IP; warn only
     * if the user explicitly set it (i.e. not the sentinel value). */
    if (videomaster_context->video_buffer_packing !=
        AV_NB_VIDEOMASTER_BUFFER_PACKINGS)
    {
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "buffer_packing is not applicable for IP 2110 mode and will "
               "be ignored. Buffer packing is determined by the stream bit "
               "depth.\n");
    }

    if (videomaster_context->dual_stream)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "dual_stream is not applicable for IP 2110 channels.\n");
        return AVERROR(EINVAL);
    }

    /* Some IP arguments are mandatory to configure IP stream */

    if (validate_network_arguments(videomaster_data, videomaster_context) !=
            0 ||
        validate_video_arguments(videomaster_data, videomaster_context) != 0)
        return AVERROR(EINVAL);
    return 0;
}

int ff_videomaster_check_audio_properties_ip(
    VideoMasterContext *videomaster_context)
{
    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Audio options are ignored in MVP IP video mode.\n");
    return 0;
}

int ff_videomaster_check_channel_integrity_ip(
    VideoMasterContext *videomaster_context)
{
    /* IP 2110 mode requires explicit parameters (auto-detect not supported) */
    videomaster_context->has_video = true;

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "IP 2110 explicit stream properties: %ux%u@%u/%u %s\n",
           videomaster_context->video_width, videomaster_context->video_height,
           videomaster_context->video_frame_rate_num,
           videomaster_context->video_frame_rate_den,
           videomaster_context->video_interlaced ? "interlaced"
                                                 : "progressive");

    if (ff_videomaster_join_multicast_group(videomaster_context) != 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to prepare IP board for main stream.\n");
        return AVERROR(EIO);
    }

    /* VHD_OpenStreamHandle in JOINED mode is not supported for IP ST2110.
     * Use VHD_OpenEssenceStreamHandle with VHD_ET_ST2110_20 instead. */
    {
        VHD_ERRORCODE open_status = (VHD_ERRORCODE)VHD_OpenEssenceStreamHandle(
            videomaster_context->board_handle, VHD_ET_ST2110_20, VHD_RX_CHANNEL,
            videomaster_context->channel_index, NULL,
            &videomaster_context->stream_handle);

        if (open_status == VHDERR_NOERROR)
        {
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "IP ST2110-20 stream handle opened successfully.\n");
            return 0;
        }

        /* Log the VHD error and channel availability at ERROR level so the
         * caller does not need -loglevel debug to diagnose the failure. */
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to open IP ST2110-20 stream handle on channel %u: "
               "%s (VHD error %d), channel status: %s.\n",
               videomaster_context->channel_index,
               VHD_ERRORCODE_ToPrettyString(open_status), (int)open_status,
               ff_videomaster_get_channel_status_ip(videomaster_context));

        if (open_status == VHDERR_STREAMUSED ||
            open_status == VHDERR_CHANNELUSED)
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Channel %u is already opened by another process. "
                   "Use 'ffmpeg -sources videomaster' to inspect channel "
                   "availability.\n",
                   videomaster_context->channel_index);
            return AVERROR(EBUSY);
        }

        return AVERROR(EIO);
    }

    return 0;
}

bool ff_videomaster_is_channel_locked_ip(
    VideoMasterContext *videomaster_context)
{
    /*
     * For IP ST2110 channels, "locked" has no equivalent of the
     * SDI/HDMI hardware signal-present bit.  VHD_ST2110_RXSTS_VIDEO_UNLOCKED
     * is only cleared after VHD_StartStream() is called with a fully
     * configured stream and packets are actually received on the network,
     * which cannot happen during a passive -sources enumeration.
     *
     * IP channels are always available by construction (the card is present
     * and the channel type is IP); the user is responsible for supplying the
     * required explicit parameters (ip_destination, ip_video_*, etc.).
     * Therefore we unconditionally report the channel as listable.
     */
    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "IP channel %u always considered available for listing\n",
           videomaster_context->channel_index);
    return true;
}

const char *
ff_videomaster_get_channel_status_ip(VideoMasterContext *videomaster_context)
{
    uint32_t availability = 0;
    uint32_t rx_index = videomaster_context->channel_index;
    uint32_t bit = (rx_index / 4) * 8 + (rx_index % 4);

    if (VHD_GetBoardProperty(videomaster_context->board_handle,
                             VHD_CORE_BP_CHN_AVAILABILITY,
                             &availability) == VHDERR_NOERROR)
        return (availability & (1u << bit)) ? "available" : "in use";

    return "available"; /* if property cannot be read, assume free */
}

uint32_t ff_videomaster_get_video_buffer_type_ip()
{
    return VHD_ST2110_BT_VIDEO;
}

/* ---- Stream setup functions ---- */

int ff_videomaster_join_multicast_group(VideoMasterContext *videomaster_context)
{
    int av_error = 0;

    if (!ip_is_multicast(videomaster_context->ip_destination))
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Destination is unicast — no multicast join needed.\n");
        return 0;
    }

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Destination is multicast — joining group on main port (ETH_0).\n");

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_SetMulticastVersion(videomaster_context->board_handle,
                                          VHD_IP_BRD_IGMP_VERSION_V3),
                  "Configured IGMPv3", "Failed to configure IGMPv3");

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_JoinMulticastGroup(videomaster_context->board_handle,
                                         VHD_IP_BRD_ETHERNETPORT_ETH_0,
                                         videomaster_context->ip_destination),
                  "Joined multicast group on main port",
                  "Failed to join multicast group on main port");

    return 0;
}

int ff_videomaster_leave_multicast_group(
    VideoMasterContext *videomaster_context)
{
    int av_error = 0;

    if (!ip_is_multicast(videomaster_context->ip_destination))
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Destination is unicast — no multicast leave needed.\n");
        return 0;
    }

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Destination is multicast — leaving group on main port (ETH_0).\n");

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_LeaveMulticastGroup(videomaster_context->board_handle,
                                          VHD_IP_BRD_ETHERNETPORT_ETH_0,
                                          videomaster_context->ip_destination),
                  "Left multicast group on main port",
                  "Failed to leave multicast group on main port");

    return 0;
}

int ff_videomaster_start_stream_ip_explicit(
    VideoMasterContext *videomaster_context)
{
    int   av_error = 0;
    ULONG ip_source = 0;
    ULONG filtering_mask = VHD_IP_FILTER_IP_ADDR_DEST;

    if (get_st2110_video_standard_from_explicit(
            videomaster_context, &videomaster_context->ip_video_standard) != 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "No ST2110-20 video standard matches %ux%u@%u/%u %s\n",
               videomaster_context->video_width,
               videomaster_context->video_height,
               videomaster_context->video_frame_rate_num,
               videomaster_context->video_frame_rate_den,
               videomaster_context->video_interlaced ? "i" : "p");
        return AVERROR(EINVAL);
    }

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_SetStreamProperty(videomaster_context->stream_handle,
                                        VHD_ST2110_20_SP_VIDEO_STANDARD,
                                        videomaster_context->ip_video_standard),
                  "Configured ST2110 video standard",
                  "Failed to configure ST2110 video standard");

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_SetStreamProperty(videomaster_context->stream_handle,
                                        VHD_ST2110_20_SP_SAMPLING,
                                        VHD_ST2110_20_SAMPLING_YUV_422),
                  "Configured ST2110 sampling (YUV 4:2:2)",
                  "Failed to configure ST2110 sampling");

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_SetStreamProperty(videomaster_context->stream_handle,
                                        VHD_ST2110_20_SP_DEPTH,
                                        videomaster_context->ip_video_depth),
                  "Configured ST2110 bit depth",
                  "Failed to configure ST2110 bit depth");

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_SetStreamProperty(videomaster_context->stream_handle,
                                        VHD_ST2110_SP_SPS_ENABLED, FALSE),
                  "SPS disabled (MVP mode)", "Failed to disable SPS");

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_SetStreamProperty(videomaster_context->stream_handle,
                                        VHD_ST2110_SP_IP_DST,
                                        videomaster_context->ip_destination),
                  "Configured ST2110 destination IP",
                  "Failed to configure ST2110 destination IP");

    /* Read source IP from main port (ETH_0) and bind it to the stream. */
    if (ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_GetEthernetPortProperty(videomaster_context->board_handle,
                                        VHD_IP_BRD_ETHERNETPORT_ETH_0,
                                        VHD_IP_BRD_EP_IP_ADDR, &ip_source),
            "Read source IPv4 from main port",
            "Failed to read source IPv4 from main port") == 0 &&
        ip_source != 0)
    {
        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_SetStreamProperty(videomaster_context->stream_handle,
                                            VHD_ST2110_SP_IP_SRC, ip_source),
                      "Configured ST2110 source IP",
                      "Failed to configure ST2110 source IP");
    }
    else if (ip_source == 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Main ethernet port has no IPv4 address set. "
               "Source IP will not be configured on stream.\n");
    }

    if (videomaster_context->ip_udp_port > 0)
    {
        filtering_mask |= VHD_IP_FILTER_UDP_PORT_DEST;
        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_SetStreamProperty(videomaster_context->stream_handle,
                                            VHD_ST2110_SP_UDP_PORT_DST,
                                            videomaster_context->ip_udp_port),
                      "Configured ST2110 UDP destination port",
                      "Failed to configure ST2110 UDP destination port");
    }

    if (videomaster_context->ip_payload_type > 0)
    {
        filtering_mask |= VHD_IP_FILTER_RTP_PAYLOAD_TYPE;
        GET_AND_CHECK(
            ff_videomaster_handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_ST2110_SP_RTP_PAYLOAD_TYPE,
                                  videomaster_context->ip_payload_type),
            "Configured ST2110 RTP payload type",
            "Failed to configure ST2110 RTP payload type");
    }

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_SetStreamProperty(videomaster_context->stream_handle,
                                        VHD_ST2110_SP_FILTERING_MASK,
                                        filtering_mask),
                  "Configured ST2110 RX filtering mask",
                  "Failed to configure ST2110 RX filtering mask");

    /* Buffer packing depends on the requested bit depth. */
    if (videomaster_context->ip_video_depth == VHD_ST2110_20_DEPTH_10BIT)
    {
        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_SetStreamProperty(videomaster_context->stream_handle,
                                            VHD_CORE_SP_BUFFER_PACKING,
                                            VHD_BUFPACK_VIDEO_YUV422_10),
                      "Configured ST2110 buffer packing (YUV422_10 / V210)",
                      "Failed to configure ST2110 buffer packing");
        videomaster_context->video_codec = AV_CODEC_ID_V210;
        videomaster_context->video_pixel_format = AV_PIX_FMT_NONE;
        /* V210: 64 bits per 6 pixels */
        videomaster_context->video_bit_rate =
            av_rescale((int64_t)videomaster_context->video_width *
                           videomaster_context->video_height * 64,
                       videomaster_context->video_frame_rate_num,
                       videomaster_context->video_frame_rate_den * 3);
    }
    else
    {
        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_SetStreamProperty(videomaster_context->stream_handle,
                                            VHD_CORE_SP_BUFFER_PACKING,
                                            VHD_BUFPACK_VIDEO_YUV422_8),
                      "Configured ST2110 buffer packing (YUV422_8)",
                      "Failed to configure ST2110 buffer packing");
        videomaster_context->video_codec = AV_CODEC_ID_RAWVIDEO;
        videomaster_context->video_pixel_format = AV_PIX_FMT_UYVY422;
        videomaster_context->video_bit_rate =
            av_rescale((int64_t)videomaster_context->video_width *
                           videomaster_context->video_height * 16,
                       videomaster_context->video_frame_rate_num,
                       videomaster_context->video_frame_rate_den);
    }

    return 0;
}

int ff_videomaster_start_stream_ip(VideoMasterContext *videomaster_context)
{
    if (videomaster_context->ip_destination != 0)
    {
        return ff_videomaster_start_stream_ip_explicit(videomaster_context);
    }
    /* Auto mode: minimal or no additional setup needed */
    return 0;
}
