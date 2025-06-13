/**
 * @file videomaster_dec.h
 * @author Pierre Perick (p.perick@deltatec.be)
 * @brief This file contains the function declaration for VideoMaster DELTACAST
 * (c) input devices.
 * @version 1.0
 * @date 2025-05-13
 *
 * @copyright Copyright (c) 2025
 *
 * This file is part of FFmpeg and use VideoMaster DELTACAST (c) API to
 * communicate with PCIe DELTACAST(c) devices.
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

#ifndef AVDEVICE_VIDEOMASTER_DEC_H
#define AVDEVICE_VIDEOMASTER_DEC_H

#include <stdbool.h>

#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"

#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_Dv.h>

/**
 * @brief Lists available VideoMaster DELTACAST(c) devices.
 *
 * Populates the device list with available devices.
 *
 * @param avctx FFmpeg context for the device.
 * @param device_list List to be populated.
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_videomaster_list_input_devices(AVFormatContext         *avctx,
                                      struct AVDeviceInfoList *device_list);

/**
 * @brief Closes the device and releases resources.
 *
 * Frees allocated resources and shuts down the device.
 *
 * @param avctx FFmpeg context for the device.
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_videomaster_read_close(AVFormatContext *avctx);

/**
 * @brief Initializes the VideoMaster DELTACAST(c) device context.
 *
 * Parses command line arguments and sets up the AVFormatContext.
 *
 * @param avctx FFmpeg context for the device.
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_videomaster_read_header(AVFormatContext *avctx);

/**
 * @brief Reads a video or audio packet from the device.
 *
 * Fills the provided AVPacket with data.
 *
 * @param avctx FFmpeg context for the device.
 * @param pkt Packet to be filled.
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_videomaster_read_packet(AVFormatContext *avctx, AVPacket *pkt);
#endif /* AVDEVICE_VIDEOMASTER_DEC_H */
