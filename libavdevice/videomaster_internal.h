/**
 * @file videomaster_internal.h
 * @brief Internal shared utilities for the VideoMaster demuxer.
 *
 * This header is NOT part of the public API. It is shared across
 * the videomaster_*.c implementation files only.
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

#ifndef AVDEVICE_VIDEOMASTER_INTERNAL_H
#define AVDEVICE_VIDEOMASTER_INTERNAL_H

#include "videomaster_common.h"

/*
 * GET_AND_CHECK — Run func(...), propagate errors immediately.
 * Requires an `int av_error` variable in the calling scope.
 */
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

/*
 * GET_AND_CHECK_AND_STOP_STREAM — Like GET_AND_CHECK but also stops the
 * stream before returning. Requires `VideoMasterContext *videomaster_context`
 * in the calling scope.
 */
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

#define CHECK_INT64_ARG_HAS_BEEN_SET(avctx, arg, arg_name, channel_type)       \
    do                                                                         \
    {                                                                          \
        if (arg < 0)                                                           \
        {                                                                      \
            av_log(avctx, AV_LOG_ERROR,                                        \
                   "Argument %s is required for %s channels.\n", arg_name,     \
                   channel_type);                                              \
            return AVERROR(EINVAL);                                            \
        }                                                                      \
    } while (0)

/**
 * @brief Translates a VHD_ERRORCODE into a FFmpeg AVERROR and logs it.
 *
 * Defined (non-static) in videomaster_common.c. Available to all
 * videomaster_*.c translation units via this header.
 *
 * @return 0 on VHDERR_NOERROR, AVERROR(EAGAIN) on timeout, AVERROR(EIO)
 * otherwise.
 */
int ff_videomaster_handle_vhd_status(AVFormatContext *avctx,
                                     VHD_ERRORCODE    vhd_status,
                                     const char      *success_message,
                                     const char      *error_message);

/**
 * @brief Propagates a pre-computed AVERROR and logs it.
 *
 * Defined (non-static) in videomaster_common.c.
 */
int ff_videomaster_handle_av_error(AVFormatContext *avctx, int av_error,
                                   const char *trace_message,
                                   const char *error_message);

/**
 * @brief Maps a logical RX channel index to its VHD_STREAMTYPE.
 *
 * Defined (non-static) in videomaster_common.c.
 *
 * @return NB_VHD_STREAMTYPES if the index is out of range.
 */
VHD_STREAMTYPE ff_videomaster_get_rx_stream_type_from_index(uint32_t index);

/**
 * @brief Converts an AVVideoMasterChannelType to a readable string.
 *
 * @param channel_type Channel type value
 * @return const char* Human-readable channel type
 */
const char *ff_videomaster_channel_type_to_string(
    enum AVVideoMasterChannelType channel_type);

#endif /* AVDEVICE_VIDEOMASTER_INTERNAL_H */
