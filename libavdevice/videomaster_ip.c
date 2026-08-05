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
#include "libavutil/file.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"

#if defined(__APPLE__)
#include <VideoMasterHD/VideoMasterHD_Core.h>
#include <VideoMasterHD/VideoMasterHD_Ip_Board.h>
#include <VideoMasterHD/VideoMasterHD_Ip_ST2110_20.h>
#include <VideoMasterHD/VideoMasterHD_Ip_ST2110_Board.h>
#include <VideoMasterHD/VideoMasterHD_SDP.h>
#else
#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_Ip_Board.h>
#include <VideoMasterHD_Ip_ST2110_20.h>
#include <VideoMasterHD_Ip_ST2110_Board.h>
#include <VideoMasterHD_SDP.h>
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
    if (videomaster_data->ip_video_destination == NULL)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Argument ip_video_destination is required for %s channels.\n",
               ff_videomaster_channel_type_to_string(
                   videomaster_context->channel_type));
        return AVERROR(EINVAL);
    }

    CHECK_INT64_ARG_HAS_BEEN_SET(videomaster_context->avctx,
                                 videomaster_data->ip_video_udp_port,
                                 "ip_video_udp_port",
                                 ff_videomaster_channel_type_to_string(
                                     videomaster_context->channel_type));

    if (videomaster_data->ip_video_sps_destination != NULL &&
        videomaster_data->ip_video_sps_udp_port < 0)
    {
        av_log(
            videomaster_context->avctx, AV_LOG_ERROR,
            "Argument ip_video_sps_udp_port is required for %s channels when "
            "ip_video_sps_destination is set.\n",
            ff_videomaster_channel_type_to_string(
                videomaster_context->channel_type));
        return AVERROR(EINVAL);
    }

    if (videomaster_data->ip_video_sps_udp_port > 0 &&
        videomaster_data->ip_video_sps_destination == NULL)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Argument ip_video_sps_destination is required for %s channels "
               "when "
               "ip_video_sps_udp_port is set.\n",
               ff_videomaster_channel_type_to_string(
                   videomaster_context->channel_type));
        return AVERROR(EINVAL);
    }

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

/* ---- SDP helpers ---- */

/**
 * @brief Extracts the IPv4 address from a VHD_SDP_IP_ADDRESS.
 *
 * Returns the raw AddressV4 field, which the SDK stores in the same byte
 * order as the board C API expects (matching the reference project convention
 * in videomaster-video-monitor). TODO: verify byte order on hardware if
 * multicast join/leave behaves unexpectedly.
 *
 * Only IPv4 is supported. The caller must validate the Version field before
 * calling this function.
 */
static uint32_t sdp_ip_to_uint32(const VHD_SDP_IP_ADDRESS *addr)
{
    return addr->AddressV4;
}

/**
 * @brief Parses an SDP file and populates the IP/ST2110 context fields.
 */
int ff_videomaster_parse_sdp_file(VideoMasterData    *videomaster_data,
                                  VideoMasterContext *videomaster_context)
{
    const char     *path = videomaster_data->ip_video_sdp_file;
    uint8_t        *buf = NULL;
    size_t          buf_size = 0;
    VHD_SDP_SESSION session;
    VHD_SDP_MEDIA   media_array[VHD_SDP_MAX_MEDIA_COUNT];
    ULONG           media_count = VHD_SDP_MAX_MEDIA_COUNT;
    ULONG           vhd_status;
    ULONG           width = 0;
    ULONG           height = 0;
    BOOL32          interlaced = FALSE;
    ULONG           frame_rate = 0;
    BOOL32          is_1001 = FALSE;
    int             ret;

    ret = av_file_map(path, &buf, &buf_size, 0, videomaster_context->avctx);
    if (ret < 0)
    {
        if (ret == AVERROR(ENOENT))
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "SDP file not found: %s\n", path);
        return ret;
    }

    vhd_status = VHD_ReadSDP((const char *)buf, (ULONG)buf_size, &session,
                             media_array, &media_count);
    av_file_unmap(buf, buf_size);

    if (vhd_status == VHDERR_BUFFERTOOSMALL)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "SDP file '%s' contains more than %d media entries and is "
               "not ST 2110-compliant.\n",
               path, VHD_SDP_MAX_MEDIA_COUNT);
        return AVERROR(EINVAL);
    }
    if (vhd_status != VHDERR_NOERROR)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to parse SDP file '%s' (VHD error %d: %s).\n", path,
               (int)vhd_status,
               VHD_ERRORCODE_ToPrettyString((VHD_ERRORCODE)vhd_status));
        return AVERROR(EINVAL);
    }

    if (media_count < 1)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "SDP file '%s' contains no media entries.\n", path);
        return AVERROR(EINVAL);
    }
    if (media_count > 2)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "SDP file '%s' contains %u media entries. A ST 2110-20 SDP "
               "must describe at most one main and one SPS stream.\n",
               path, (unsigned)media_count);
        return AVERROR(EINVAL);
    }

    /* Validate media types */
    if (media_array[0].MediaType != VHD_SDP_MEDIA_TYPE_ST2110_20)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "First media entry in SDP '%s' is not ST2110-20 (type %d).\n",
               path, (int)media_array[0].MediaType);
        return AVERROR(EINVAL);
    }
    if (media_count == 2 &&
        media_array[1].MediaType != VHD_SDP_MEDIA_TYPE_ST2110_20)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Second media entry (SPS) in SDP '%s' is not ST2110-20 "
               "(type %d).\n",
               path, (int)media_array[1].MediaType);
        return AVERROR(EINVAL);
    }

    /* IPv6 is not supported */
    if (media_array[0].DestinationIP.Version != VHD_SDP_IP_VERSION_4)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "IPv6 destinations are not supported (main stream in '%s').\n",
               path);
        return AVERROR(EINVAL);
    }
    if (media_count == 2 &&
        media_array[1].DestinationIP.Version != VHD_SDP_IP_VERSION_4)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "IPv6 destinations are not supported (SPS stream in '%s').\n",
               path);
        return AVERROR(EINVAL);
    }

    /* Reverse-look-up video dimensions from the parsed video standard */
    vhd_status = VHD_ST2110_20_GetVideoCharacteristics(
        media_array[0].ST2110_20.VideoStandard, &width, &height, &interlaced,
        &frame_rate, &is_1001);
    if (vhd_status != VHDERR_NOERROR)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to retrieve video characteristics for ST2110-20 "
               "standard %d from SDP '%s' (VHD error %d).\n",
               (int)media_array[0].ST2110_20.VideoStandard, path,
               (int)vhd_status);
        return AVERROR(EINVAL);
    }

    /* Populate context */
    videomaster_context->ip_video_sdp_mode = true;
    videomaster_context->ip_video_sdp_session = session;
    videomaster_context->ip_video_sdp_media[0] = media_array[0];
    videomaster_context->ip_video_sdp_media_count = media_count;

    videomaster_context->ip_video_standard =
        media_array[0].ST2110_20.VideoStandard;
    videomaster_context->ip_video_depth = media_array[0].ST2110_20.Depth;

    videomaster_context->video_width = (uint32_t)width;
    videomaster_context->video_height = (uint32_t)height;
    videomaster_context->video_interlaced = !!interlaced;
    videomaster_context->video_frame_rate_num = (uint32_t)frame_rate * 1000;
    videomaster_context->video_frame_rate_den = is_1001 ? 1001 : 1000;

    videomaster_context->ip_video_destination = sdp_ip_to_uint32(
        &media_array[0].DestinationIP);
    videomaster_context->ip_video_udp_port = (uint32_t)media_array[0].UdpPort;
    videomaster_context->ip_video_payload_type =
        (uint32_t)media_array[0].PayloadType;

    /* Source IP: use session origin for unicast streams */
    if (!ip_is_multicast(videomaster_context->ip_video_destination) &&
        session.SourceIP.Version == VHD_SDP_IP_VERSION_4 &&
        session.SourceIP.AddressV4 != 0)
    {
        videomaster_context->ip_video_source = sdp_ip_to_uint32(
            &session.SourceIP);
    }

    if (media_count == 2)
    {
        videomaster_context->ip_video_sdp_media[1] = media_array[1];
        videomaster_context->ip_video_sps_destination = sdp_ip_to_uint32(
            &media_array[1].DestinationIP);
        videomaster_context->ip_video_sps_udp_port =
            (uint32_t)media_array[1].UdpPort;
        videomaster_context->ip_video_sps_payload_type =
            (uint32_t)media_array[1].PayloadType;
    }

    return 0;
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

    /* SDP mode: all fields are already populated from the file. */
    if (videomaster_context->ip_video_sdp_mode)
        return 0;

    /* Explicit mode: validate mandatory network and video arguments. */
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
           "IP 2110 %s stream properties: %ux%u@%u/%u %s\n",
           videomaster_context->ip_video_sdp_mode ? "SDP" : "explicit",
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
     * required explicit parameters (ip_video_destination, ip_video_*, etc.).
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

/**
 * @brief Joins multicast groups for a single media entry (SDP mode).
 *
 * For multicast destinations only. Applies SSM source filtering when the
 * SDP source-filter attribute is present.
 */
static int
join_multicast_group_sdp_entry(VideoMasterContext     *videomaster_context,
                               const VHD_SDP_MEDIA    *media,
                               VHD_IP_BRD_ETHERNETPORT eth_port)
{
    int      av_error = 0;
    uint32_t addr = sdp_ip_to_uint32(&media->DestinationIP);

    if (!ip_is_multicast(addr))
    {
        av_log(videomaster_context->avctx, AV_LOG_DEBUG,
               "SDP: destination %u.%u.%u.%u is unicast — skipping multicast "
               "join on ETH_%d.\n",
               (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
               addr & 0xFF, (int)eth_port);
        return 0;
    }

    av_log(videomaster_context->avctx, AV_LOG_DEBUG,
           "SDP: joining multicast group %u.%u.%u.%u on ETH_%d%s.\n",
           (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
           addr & 0xFF, (int)eth_port,
           media->SourceFilter.UseSourceFilter ? " (SSM)" : " (ASM)");

    /* The VHD SDK requires the group to be joined before sources can be
     * added (VHD_AddMulticastSource requires a prior VHD_JoinMulticastGroup).
     */
    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_JoinMulticastGroup(videomaster_context->board_handle,
                                         eth_port, addr),
                  "Joined SDP multicast group",
                  "Failed to join SDP multicast group");

    if (media->SourceFilter.UseSourceFilter)
    {
        VHD_IP_BRD_FILTERMODEMULTICAST filter_mode =
            (media->SourceFilter.FilterMode == VHD_SDP_FILTER_MODE_INCL)
                ? VHD_IP_BRD_FILTERMODEMULTICAST_INCLUDE
                : VHD_IP_BRD_FILTERMODEMULTICAST_EXCLUDE;

        av_log(videomaster_context->avctx, AV_LOG_INFO,
               "SDP SSM: applying %s filter on group %u.%u.%u.%u ETH_%d: "
               "%u source(s).\n",
               media->SourceFilter.FilterMode == VHD_SDP_FILTER_MODE_INCL
                   ? "INCLUDE"
                   : "EXCLUDE",
               (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF,
               addr & 0xFF, (int)eth_port,
               (unsigned)media->SourceFilter.SourceIPCount);

        for (ULONG i = 0; i < media->SourceFilter.SourceIPCount; i++)
        {
            uint32_t src = sdp_ip_to_uint32(
                &media->SourceFilter.SourceIPArray[i]);
            av_log(videomaster_context->avctx, AV_LOG_DEBUG,
                   "SDP SSM: adding source %u.%u.%u.%u for group "
                   "%u.%u.%u.%u on ETH_%d.\n",
                   (src >> 24) & 0xFF, (src >> 16) & 0xFF, (src >> 8) & 0xFF,
                   src & 0xFF, (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                   (addr >> 8) & 0xFF, addr & 0xFF, (int)eth_port);
            GET_AND_CHECK(
                ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                videomaster_context->avctx,
                VHD_AddMulticastSource(videomaster_context->board_handle,
                                       eth_port, addr, src),
                "Added SDP SSM source", "Failed to add SDP SSM source");
        }

        GET_AND_CHECK(
            ff_videomaster_handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_SetMulticastFilterMode(videomaster_context->board_handle,
                                       eth_port, addr, filter_mode),
            "Set SDP SSM filter mode", "Failed to set SDP SSM filter mode");
    }

    return 0;
}

int ff_videomaster_join_multicast_group(VideoMasterContext *videomaster_context)
{
    int av_error = 0;

    if (videomaster_context->ip_video_sdp_mode)
    {
        static const VHD_IP_BRD_ETHERNETPORT eth_ports[] = {
            VHD_IP_BRD_ETHERNETPORT_ETH_0,
            VHD_IP_BRD_ETHERNETPORT_ETH_1,
        };

        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_SetMulticastVersion(videomaster_context->board_handle,
                                              VHD_IP_BRD_IGMP_VERSION_V3),
                      "Configured IGMPv3", "Failed to configure IGMPv3");

        for (ULONG i = 0; i < videomaster_context->ip_video_sdp_media_count;
             i++)
        {
            GET_AND_CHECK(join_multicast_group_sdp_entry,
                          videomaster_context->avctx, videomaster_context,
                          &videomaster_context->ip_video_sdp_media[i],
                          eth_ports[i]);
        }
        return 0;
    }

    if (!ip_is_multicast(videomaster_context->ip_video_destination))
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

    GET_AND_CHECK(
        ff_videomaster_handle_vhd_status, videomaster_context->avctx,
        videomaster_context->avctx,
        VHD_JoinMulticastGroup(videomaster_context->board_handle,
                               VHD_IP_BRD_ETHERNETPORT_ETH_0,
                               videomaster_context->ip_video_destination),
        "Joined multicast group on main port",
        "Failed to join multicast group on main port");

    if (videomaster_context->ip_video_sps_destination != 0 &&
        ip_is_multicast(videomaster_context->ip_video_sps_destination))
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "SPS destination is multicast — joining group on secondary port "
               "(ETH_1).\n");

        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_JoinMulticastGroup(
                          videomaster_context->board_handle,
                          VHD_IP_BRD_ETHERNETPORT_ETH_1,
                          videomaster_context->ip_video_sps_destination),
                      "Joined SPS multicast group on secondary port",
                      "Failed to join SPS multicast group on secondary port");
    }

    return 0;
}

int ff_videomaster_leave_multicast_group(
    VideoMasterContext *videomaster_context)
{
    int av_error = 0;

    if (videomaster_context->ip_video_sdp_mode)
    {
        static const VHD_IP_BRD_ETHERNETPORT eth_ports[] = {
            VHD_IP_BRD_ETHERNETPORT_ETH_0,
            VHD_IP_BRD_ETHERNETPORT_ETH_1,
        };

        for (ULONG i = 0; i < videomaster_context->ip_video_sdp_media_count;
             i++)
        {
            uint32_t addr = sdp_ip_to_uint32(
                &videomaster_context->ip_video_sdp_media[i].DestinationIP);
            if (!ip_is_multicast(addr))
                continue;
            GET_AND_CHECK(
                ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                videomaster_context->avctx,
                VHD_LeaveMulticastGroup(videomaster_context->board_handle,
                                        eth_ports[i], addr),
                "Left SDP multicast group",
                "Failed to leave SDP multicast group");
        }
        return 0;
    }

    if (!ip_is_multicast(videomaster_context->ip_video_destination))
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Destination is unicast — no multicast leave needed.\n");
        return 0;
    }

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Destination is multicast — leaving group on main port (ETH_0).\n");

    GET_AND_CHECK(
        ff_videomaster_handle_vhd_status, videomaster_context->avctx,
        videomaster_context->avctx,
        VHD_LeaveMulticastGroup(videomaster_context->board_handle,
                                VHD_IP_BRD_ETHERNETPORT_ETH_0,
                                videomaster_context->ip_video_destination),
        "Left multicast group on main port",
        "Failed to leave multicast group on main port");

    if (videomaster_context->ip_video_sps_destination != 0 &&
        ip_is_multicast(videomaster_context->ip_video_sps_destination))
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "SPS destination is multicast — leaving group on secondary port "
               "(ETH_1).\n");
        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_LeaveMulticastGroup(
                          videomaster_context->board_handle,
                          VHD_IP_BRD_ETHERNETPORT_ETH_1,
                          videomaster_context->ip_video_sps_destination),
                      "Left SPS multicast group on secondary port",
                      "Failed to leave SPS multicast group on secondary port");
    }

    return 0;
}

static int set_buffer_packing_and_codec(VideoMasterContext *ctx)
{
    int av_error = 0;

    if (ctx->ip_video_depth == VHD_ST2110_20_DEPTH_10BIT)
    {
        GET_AND_CHECK(ff_videomaster_handle_vhd_status, ctx->avctx, ctx->avctx,
                      VHD_SetStreamProperty(ctx->stream_handle,
                                            VHD_CORE_SP_BUFFER_PACKING,
                                            VHD_BUFPACK_VIDEO_YUV422_10),
                      "Configured ST2110 buffer packing (YUV422_10 / V210)",
                      "Failed to configure ST2110 buffer packing");
        ctx->video_codec = AV_CODEC_ID_V210;
        ctx->video_pixel_format = AV_PIX_FMT_NONE;
        /* V210: 64 bits per 6 pixels */
        ctx->video_bit_rate = av_rescale((int64_t)ctx->video_width *
                                             ctx->video_height * 64,
                                         ctx->video_frame_rate_num,
                                         ctx->video_frame_rate_den * 3);
    }
    else
    {
        GET_AND_CHECK(ff_videomaster_handle_vhd_status, ctx->avctx, ctx->avctx,
                      VHD_SetStreamProperty(ctx->stream_handle,
                                            VHD_CORE_SP_BUFFER_PACKING,
                                            VHD_BUFPACK_VIDEO_YUV422_8),
                      "Configured ST2110 buffer packing (YUV422_8)",
                      "Failed to configure ST2110 buffer packing");
        ctx->video_codec = AV_CODEC_ID_RAWVIDEO;
        ctx->video_pixel_format = AV_PIX_FMT_UYVY422;
        ctx->video_bit_rate = av_rescale((int64_t)ctx->video_width *
                                             ctx->video_height * 16,
                                         ctx->video_frame_rate_num,
                                         ctx->video_frame_rate_den);
    }

    return 0;
}

int ff_videomaster_start_stream_ip(VideoMasterContext *videomaster_context)
{
    int   av_error = 0;
    ULONG filtering_mask = 0;
    bool  has_sps = videomaster_context->ip_video_sps_destination != 0;

    /* In explicit mode, ip_video_standard is not yet resolved */
    if (!videomaster_context->ip_video_sdp_mode)
    {
        if (get_st2110_video_standard_from_explicit(
                videomaster_context, &videomaster_context->ip_video_standard) !=
            0)
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
                                        VHD_ST2110_SP_SPS_ENABLED,
                                        has_sps ? TRUE : FALSE),
                  has_sps ? "Enabled ST2110 SPS" : "Disabled ST2110 SPS",
                  "Failed to configure ST2110 SPS enable");

    if (videomaster_context->ip_video_destination != 0)
    {
        filtering_mask |= VHD_IP_FILTER_IP_ADDR_DEST;
        GET_AND_CHECK(
            ff_videomaster_handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_ST2110_SP_IP_DST,
                                  videomaster_context->ip_video_destination),
            "Configured ST2110 destination IP",
            "Failed to configure ST2110 destination IP");
    }

    if (videomaster_context->ip_video_udp_port > 0)
    {
        filtering_mask |= VHD_IP_FILTER_UDP_PORT_DEST;
        GET_AND_CHECK(
            ff_videomaster_handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_ST2110_SP_UDP_PORT_DST,
                                  videomaster_context->ip_video_udp_port),
            "Configured ST2110 UDP destination port",
            "Failed to configure ST2110 UDP destination port");
    }

    filtering_mask |= VHD_IP_FILTER_RTP_PAYLOAD_TYPE;
    GET_AND_CHECK(
        ff_videomaster_handle_vhd_status, videomaster_context->avctx,
        videomaster_context->avctx,
        VHD_SetStreamProperty(videomaster_context->stream_handle,
                              VHD_ST2110_SP_RTP_PAYLOAD_TYPE,
                              videomaster_context->ip_video_payload_type),
        "Configured ST2110 RTP payload type",
        "Failed to configure ST2110 RTP payload type");

    if (videomaster_context->ip_video_source != 0)
    {
        filtering_mask |= VHD_IP_FILTER_IP_ADDR_SRC;
        GET_AND_CHECK(
            ff_videomaster_handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_ST2110_SP_IP_SRC,
                                  videomaster_context->ip_video_source),
            "Configured ST2110 source IP",
            "Failed to configure ST2110 source IP");
    }

    if (videomaster_context->ip_video_udp_port_src > 0)
    {
        filtering_mask |= VHD_IP_FILTER_UDP_PORT_SRC;
        GET_AND_CHECK(
            ff_videomaster_handle_vhd_status, videomaster_context->avctx,
            videomaster_context->avctx,
            VHD_SetStreamProperty(videomaster_context->stream_handle,
                                  VHD_ST2110_SP_UDP_PORT_SRC,
                                  videomaster_context->ip_video_udp_port_src),
            "Configured ST2110 UDP source port",
            "Failed to configure ST2110 UDP source port");
    }

    GET_AND_CHECK(ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                  videomaster_context->avctx,
                  VHD_SetStreamProperty(videomaster_context->stream_handle,
                                        VHD_ST2110_SP_FILTERING_MASK,
                                        filtering_mask),
                  "Configured ST2110 RX filtering mask",
                  "Failed to configure ST2110 RX filtering mask");

    if (has_sps)
    {
        ULONG sps_filtering_mask = VHD_IP_FILTER_IP_ADDR_DEST |
                                   VHD_IP_FILTER_RTP_PAYLOAD_TYPE;

        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_SetStreamProperty(
                          videomaster_context->stream_handle,
                          VHD_ST2110_SP_SPS_IP_DST,
                          videomaster_context->ip_video_sps_destination),
                      "Configured ST2110 SPS destination IP",
                      "Failed to configure ST2110 SPS destination IP");

        if (videomaster_context->ip_video_sps_udp_port > 0)
        {
            sps_filtering_mask |= VHD_IP_FILTER_UDP_PORT_DEST;
            GET_AND_CHECK(
                ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                videomaster_context->avctx,
                VHD_SetStreamProperty(
                    videomaster_context->stream_handle,
                    VHD_ST2110_SP_SPS_UDP_PORT_DST,
                    videomaster_context->ip_video_sps_udp_port),
                "Configured ST2110 SPS UDP destination port",
                "Failed to configure ST2110 SPS UDP destination port");
        }

        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_SetStreamProperty(
                          videomaster_context->stream_handle,
                          VHD_ST2110_SP_SPS_RTP_PAYLOAD_TYPE,
                          videomaster_context->ip_video_sps_payload_type),
                      "Configured ST2110 SPS RTP payload type",
                      "Failed to configure ST2110 SPS RTP payload type");

        /* SPS source IP/port: explicit mode only; SDP SSM handled in
         * join_multicast_group */
        if (videomaster_context->ip_video_sps_source != 0)
        {
            sps_filtering_mask |= VHD_IP_FILTER_IP_ADDR_SRC;
            GET_AND_CHECK(
                ff_videomaster_handle_vhd_status, videomaster_context->avctx,
                videomaster_context->avctx,
                VHD_SetStreamProperty(videomaster_context->stream_handle,
                                      VHD_ST2110_SP_SPS_IP_SRC,
                                      videomaster_context->ip_video_sps_source),
                "Configured ST2110 SPS source IP",
                "Failed to configure ST2110 SPS source IP");
        }

        if (videomaster_context->ip_video_sps_udp_port_src > 0)
        {
            sps_filtering_mask |= VHD_IP_FILTER_UDP_PORT_SRC;
            GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                          videomaster_context->avctx,
                          videomaster_context->avctx,
                          VHD_SetStreamProperty(
                              videomaster_context->stream_handle,
                              VHD_ST2110_SP_SPS_UDP_PORT_SRC,
                              videomaster_context->ip_video_sps_udp_port_src),
                          "Configured ST2110 SPS UDP source port",
                          "Failed to configure ST2110 SPS UDP source port");
        }

        GET_AND_CHECK(ff_videomaster_handle_vhd_status,
                      videomaster_context->avctx, videomaster_context->avctx,
                      VHD_SetStreamProperty(videomaster_context->stream_handle,
                                            VHD_ST2110_SP_SPS_FILTERING_MASK,
                                            sps_filtering_mask),
                      "Configured ST2110 SPS RX filtering mask",
                      "Failed to configure ST2110 SPS RX filtering mask");

        av_log(videomaster_context->avctx, AV_LOG_INFO, "Enabled ST2110 SPS\n");
    }

    return set_buffer_packing_and_codec(videomaster_context);
}

int ff_videomaster_lock_next_slot_ip(VideoMasterContext *ctx,
                                     uint8_t **video_buf, uint32_t *video_size,
                                     uint8_t **audio_buf, uint32_t *audio_size,
                                     void    **slot_to_unlock)
{
    /* Phase 3 will implement sync-handle and audio-essence paths */
    int ret = ff_videomaster_get_data(ctx);
    if (ret != 0)
        return ret;
    *video_buf      = ctx->video_buffer;
    *video_size     = ctx->video_buffer_size;
    *audio_buf      = ctx->audio_buffer;
    *audio_size     = ctx->audio_buffer_size;
    *slot_to_unlock = ctx->slot_handle;
    return 0;
}

int ff_videomaster_unlock_slot_ip(VideoMasterContext *ctx, void *slot)
{
    (void)slot;
    return ff_videomaster_release_data(ctx);
}
