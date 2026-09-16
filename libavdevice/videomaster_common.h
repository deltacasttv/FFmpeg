/**
 * @file videomaster_common.h
 * @author Pierre Perick (p.perick@deltatec.be)
 * @brief This file contains the function declaration use to manage VideoMaster
 * DELTACAST (c) devices.
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

#ifndef AVDEVICE_VIDEOMASTER_COMMON_H
#define AVDEVICE_VIDEOMASTER_COMMON_H

#include "libavcodec/avcodec.h"
#include "libavcodec/packet_internal.h"
#include "libavdevice/avdevice.h"
#include "libavutil/avutil.h"
#include "libavutil/thread.h"

#if defined(__APPLE__)
#include <VideoMasterHD/VideoMasterHD_Core.h>
#include <VideoMasterHD/VideoMasterHD_Dv.h>
#include <VideoMasterHD/VideoMasterHD_Dv_Audio.h>
#include <VideoMasterHD/VideoMasterHD_Ip_Board.h>
#include <VideoMasterHD/VideoMasterHD_Ip_ST2110_20.h>
#include <VideoMasterHD/VideoMasterHD_Ip_ST2110_Board.h>
#include <VideoMasterHD/VideoMasterHD_SDP.h>
#include <VideoMasterHD/VideoMasterHD_Sdi.h>
#include <VideoMasterHD/VideoMasterHD_Sdi_Audio.h>
#include <VideoMasterHD/VideoMasterHD_String.h>
#else
#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_Dv.h>
#include <VideoMasterHD_Dv_Audio.h>
#include <VideoMasterHD_Ip_Board.h>
#include <VideoMasterHD_Ip_ST2110_20.h>
#include <VideoMasterHD_Ip_ST2110_Board.h>
#include <VideoMasterHD_SDP.h>
#include <VideoMasterHD_Sdi.h>
#include <VideoMasterHD_Sdi_Audio.h>
#include <VideoMasterHD_String.h>
#endif
#include <stdbool.h>

/**
 * @brief Enumeration of VideoMaster channel types.
 *
 * This enumeration defines the various channel types available for
 * VideoMaster devices.
 */
enum AVVideoMasterChannelType
{
    AV_VIDEOMASTER_CHANNEL_HDMI,
    AV_VIDEOMASTER_CHANNEL_ASISDI,
    AV_VIDEOMASTER_CHANNEL_SDI,
    AV_VIDEOMASTER_CHANNEL_IP_2110,
    AV_VIDEOMASTER_CHANNEL_UNKNOWN
};

/**
 * @brief Enumeration of VideoMaster sample rates.
 *
 * This enumeration defines the various sample rates available for
 * VideoMaster audio streams. Each value corresponds to a specific sample rate
 * in Hz.
 */
enum AVVideoMasterSampleRateValue
{
    AV_VIDEOMASTER_SAMPLE_RATE_32000 = 32000,
    AV_VIDEOMASTER_SAMPLE_RATE_44100 = 44100,
    AV_VIDEOMASTER_SAMPLE_RATE_48000 = 48000,
    AV_VIDEOMASTER_SAMPLE_RATE_UNKNOWN = 0
};

/**
 * @brief Enumeration of VideoMaster sample sizes.
 *
 * This enumeration defines the various sample sizes available for
 * VideoMaster audio streams. Each value corresponds to a specific bit depth
 */
enum AVVideoMasterSampleSizeValue
{
    AV_VIDEOMASTER_SAMPLE_SIZE_16 = 16,
    AV_VIDEOMASTER_SAMPLE_SIZE_24 = 24,
    AV_VIDEOMASTER_SAMPLE_SIZE_UNKNOWN = 0
};

/**
 * @brief Enumeration of VideoMaster timestamp types.
 *
 * This enumeration defines the various timestamp sources available for
 * VideoMaster devices.
 */
enum AVVideoMasterTimeStampType
{
    AV_VIDEOMASTER_TIMESTAMP_OSCILLATOR,
    AV_VIDEOMASTER_TIMESTAMP_SYSTEM,
    AV_VIDEOMASTER_TIMESTAMP_HARDWARE,
    AV_VIDEOMASTER_TIMESTAMP_LTC_COMPANION_CARD,
    AV_VIDEOMASTER_TIMESTAMP_LTC_ON_BOARD,
    AV_VIDEOMASTER_TIMESTAMP_RTP,  ///< IP (ST2110) only: per-slot RTP media
                                   ///< clock timestamp
    AV_VIDEOMASTER_TIMESTAMP_PTP,  ///< IP only: the board's absolute PTP
                                   ///< time (live read, not per-slot)
    AV_VIDEOMASTER_TIMESTAMP_NB
};

enum AVVideoMasterBufferPacking
{
    AV_VIDEOMASTER_BUFFER_PACKING_YUV422_8 = VHD_BUFPACK_VIDEO_YUV422_8,
    AV_VIDEOMASTER_BUFFER_PACKING_YUVK4224_8 = VHD_BUFPACK_VIDEO_YUVK4224_8,
    AV_VIDEOMASTER_BUFFER_PACKING_YUV422_10 = VHD_BUFPACK_VIDEO_YUV422_10,
    AV_VIDEOMASTER_BUFFER_PACKING_YUVK4224_10 = VHD_BUFPACK_VIDEO_YUVK4224_10,
    AV_VIDEOMASTER_BUFFER_PACKING_YUV4444_8 = VHD_BUFPACK_VIDEO_YUV4444_8,
    AV_VIDEOMASTER_BUFFER_PACKING_YUVK4444_8 = VHD_BUFPACK_VIDEO_YUVK4444_8,
    AV_VIDEOMASTER_BUFFER_PACKING_YUV444_10 = VHD_BUFPACK_VIDEO_YUV444_10,
    AV_VIDEOMASTER_BUFFER_PACKING_YUVK4444_10 = VHD_BUFPACK_VIDEO_YUVK4444_10,
    AV_VIDEOMASTER_BUFFER_PACKING_RGB_32 = VHD_BUFPACK_VIDEO_RGB_32,
    AV_VIDEOMASTER_BUFFER_PACKING_RGBA_32 = VHD_BUFPACK_VIDEO_RGBA_32,
    AV_VIDEOMASTER_BUFFER_PACKING_RGB_24 = VHD_BUFPACK_VIDEO_RGB_24,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU420_8 =
        VHD_BUFPACK_VIDEO_PLANAR_YVU420_8,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV420_8 =
        VHD_BUFPACK_VIDEO_PLANAR_YUV420_8,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU420_10_MSB_PAD =
        VHD_BUFPACK_VIDEO_PLANAR_YVU420_10_MSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU420_10_LSB_PAD =
        VHD_BUFPACK_VIDEO_PLANAR_YVU420_10_LSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV420_10_MSB_PAD =
        VHD_BUFPACK_VIDEO_PLANAR_YUV420_10_MSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV420_10_LSB_PAD =
        VHD_BUFPACK_VIDEO_PLANAR_YUV420_10_LSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_RGB_64 = VHD_BUFPACK_VIDEO_RGB_64,
    AV_VIDEOMASTER_BUFFER_PACKING_YUV422_16 = VHD_BUFPACK_VIDEO_YUV422_16,
    AV_VIDEOMASTER_BUFFER_PACKING_YUV444_8 = VHD_BUFPACK_VIDEO_YUV444_8,
    AV_VIDEOMASTER_BUFFER_PACKING_ICTCP_422_8 = VHD_BUFPACK_VIDEO_ICTCP_422_8,
    AV_VIDEOMASTER_BUFFER_PACKING_ICTCP_422_10 = VHD_BUFPACK_VIDEO_ICTCP_422_10,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV422_10_LSB_PAD =
        VHD_BUFPACK_VIDEO_PLANAR_YUV422_10_LSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV422_10_MSB_PAD =
        VHD_BUFPACK_VIDEO_PLANAR_YUV422_10_MSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU422_10_LSB_PAD =
        VHD_BUFPACK_VIDEO_PLANAR_YVU422_10_LSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU422_10_MSB_PAD =
        VHD_BUFPACK_VIDEO_PLANAR_YVU422_10_MSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YUV422_8 =
        VHD_BUFPACK_VIDEO_PLANAR_YUV422_8,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_YVU422_8 =
        VHD_BUFPACK_VIDEO_PLANAR_YVU422_8,
    AV_VIDEOMASTER_BUFFER_PACKING_YUV422_10_NOPAD_BIGEND =
        VHD_BUFPACK_VIDEO_YUV422_10_NOPAD_BIGEND,
    AV_VIDEOMASTER_BUFFER_PACKING_PALETTE_RGBA_8 =
        VHD_BUFPACK_VIDEO_PALETTE_RGBA_8,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_NV12 = VHD_BUFPACK_VIDEO_PLANAR_NV12,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_RGB444_10_LSB_PAD =
        VHD_BUFPACK_VIDEO_RGB444_10_LSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_RGBA4444_10_LSB_PAD =
        VHD_BUFPACK_VIDEO_RGBA4444_10_LSB_PAD,
    AV_VIDEOMASTER_BUFFER_PACKING_RGBA4444_16 = VHD_BUFPACK_VIDEO_RGBA4444_16,
    AV_VIDEOMASTER_BUFFER_PACKING_PLANAR_P010 = VHD_BUFPACK_VIDEO_PLANAR_P010,
    AV_NB_VIDEOMASTER_BUFFER_PACKINGS
};

/**
 * @brief Union to hold video and audio information for VideoMaster streams.
 */
union VideoMasterVideoInfo
{
    struct
    {

        uint32_t  pixel_clock;  ///< pixel clock of the video stream
        VHD_DV_CS color_space;  ///< color space of the video stream
        VHD_DV_SAMPLING
        cable_bit_sampling;     ///< cable bit sampling of the video stream
        uint32_t refresh_rate;  ///< refresh rate of the video stream
    } hdmi;

    struct
    {
        VHD_VIDEOSTANDARD
        video_standard;                  ///< video standard of the video stream
        VHD_CLOCKDIVISOR clock_divisor;  ///< clock divisor for the video stream
        VHD_INTERFACE    interface;      ///< interface type of the video stream
    } sdi;
};

/**
 * @brief Union to hold audio information for VideoMaster streams.
 */
union VideoMasterAudioInfo
{
    struct
    {
        VHD_DVAUDIOFORMAT
        format;  ///< audio format of the audio stream (usage only for PCM
                 ///< extraction)
    } hdmi;

    struct
    {

        VHD_AUDIOINFO audio_info;  ///< audio information for the audio stream
    } sdi;
};

/**
 * @brief Thread-safe FIFO of AVPackets.
 *
 * Used by the IP non-sync combined mode's dedicated audio capture thread
 * (see videomaster_ip.c) to hand off audio AVPackets to read_packet(),
 * decoupling the audio essence's real arrival rate from however often
 * read_packet() is called for video. Bounded by max_q_size (bytes); packets
 * are dropped (with a warning) rather than blocking the producer once full.
 */
typedef struct VideoMasterPacketQueue
{
    PacketList         pkt_list;
    int                nb_packets;
    unsigned long long size;
    int                abort_request;
    AVMutex            mutex;
    AVCond             cond;
    AVFormatContext   *avctx;
    int64_t            max_q_size;
} VideoMasterPacketQueue;

/**
 * @brief Main operational context for the VideoMaster DELTACAST(c) device
 * integration.
 *
 * This structure maintains the complete state required for interacting with
 * VideoMaster hardware devices. It stores device handles, configuration
 * parameters, stream properties, buffers, and statistics for both video and
 * audio streams.
 *
 * The context is created during device initialization and used throughout all
 * operations. It tracks both hardware state (handles, capabilities) and media
 * properties (frame rates, formats, codecs) necessary for FFmpeg integration.
 *
 * Various videomaster functions update fields in this structure as the device
 * operates, including frame counters, stream properties, and buffer management.
 */
typedef struct VideoMasterContext
{
    void *board_handle;   ///> handle to the board
    void *stream_handle;  ///> handle to the stream
    void *slot_handle;    ///> handle to the slot

    uint32_t board_index;    ///< index of the board to use
    uint32_t channel_index;  ///< index of the stream to use
    enum AVVideoMasterChannelType
        channel_type;  ///< type of the channel (HDMI or SDI)
    enum AVVideoMasterTimeStampType
        timestamp_source;  ///< source of the timestamp (video essence, and
                           ///< SDI/HDMI's single shared essence)
    enum AVVideoMasterTimeStampType
         audio_timestamp_source;  ///< source of the timestamp for the IP
                                  ///< audio essence (resolved: falls back to
                                  ///< timestamp_source when not explicitly
                                  ///< set via ip_audio_timestamp_source)
    bool dual_stream;  ///< true if the stream must be configured with 3G-B-DS
                       ///< interface

    uint32_t api_version;       ///< API version
    uint32_t number_of_boards;  ///< number of boards detected
    uint32_t nb_rx_channels;    ///< number of RX channels
    uint32_t nb_tx_channels;    ///< number of TX channels

    // video stream data
    bool has_video;  ///< true if the stream has video data

    union VideoMasterVideoInfo
        video_info;  ///< video information for the stream

    uint32_t video_width;   ///< width of the video stream
    uint32_t video_height;  ///< height of the video stream
    uint32_t
        video_frame_rate_num;  ///< base for the frame rate of the video stream
    uint32_t video_frame_rate_den;   ///< denominator for the frame rate of the
                                     ///< video stream
    bool     video_interlaced;       ///< interlaced mode of the video stream
    bool video_needs_field_reorder;  ///< frame buffer is top-half/bottom-half
    enum AVCodecID video_codec;      ///< codec ID of the video stream
    enum AVPixelFormat
             video_pixel_format;  ///< pixel format of the video stream
    uint32_t video_bit_rate;      ///< bit rate of the video stream
    enum AVVideoMasterBufferPacking
        video_buffer_packing;  ///< buffer packing format

    /* IP ST2110 explicit mode fields (video essence).
     * Main port is always VHD_IP_BRD_ETHERNETPORT_ETH_0 for the main
     * stream. SPS will use VHD_IP_BRD_ETHERNETPORT_ETH_1 when added.
     */
    uint32_t                     ip_video_destination;
    uint32_t                     ip_video_sps_destination;
    uint32_t                     ip_video_udp_port;
    uint32_t                     ip_video_sps_udp_port;
    uint32_t                     ip_video_source;
    uint32_t                     ip_video_sps_source;
    uint32_t                     ip_video_udp_port_src;
    uint32_t                     ip_video_sps_udp_port_src;
    uint32_t                     ip_video_payload_type;
    uint32_t                     ip_video_sps_payload_type;
    VHD_ST2110_20_VIDEO_STANDARD ip_video_standard;
    VHD_ST2110_20_DEPTH          ip_video_depth;

    /* SDP mode fields for the video essence. */
    bool            ip_video_sdp_mode;
    VHD_SDP_SESSION ip_video_sdp_session;
    VHD_SDP_MEDIA   ip_video_sdp_media[2];  ///< [0]=main stream, [1]=SPS stream
    ULONG           ip_video_sdp_media_count;

    /* IP ST2110-30 audio essence fields. */
    void                     *ip_audio_stream_handle;
    uint32_t                  ip_audio_destination;
    uint32_t                  ip_audio_udp_port;
    uint32_t                  ip_audio_source;
    uint32_t                  ip_audio_udp_port_src;
    uint32_t                  ip_audio_payload_type;
    uint32_t                  ip_audio_sps_destination;
    uint32_t                  ip_audio_sps_udp_port;
    uint32_t                  ip_audio_sps_source;
    uint32_t                  ip_audio_sps_udp_port_src;
    uint32_t                  ip_audio_sps_payload_type;
    uint32_t                  ip_audio_channel_index;
    VHD_ST2110_30_FORMAT      ip_audio_format;
    VHD_ST2110_30_PACKET_TIME ip_audio_packet_time;
    bool                      ip_audio_sdp_mode;
    VHD_SDP_MEDIA ip_audio_sdp_media;  ///< parsed audio SDP entry (SSM source
                                       ///< filter included)
    void         *ip_sync_handle;
    bool  ip_sync_mode;  ///< true when video+audio synced via StreamSyncHandle
    void *ip_audio_slot_handle;  ///< locked slot for audio-only / non-sync path

    AVPacket *pending_packet;  ///< audio packet buffered from the current slot
    float     ltc_frame_rate;  ///< frame rate for LTC timestamp calculation

    // audio stream data
    bool           has_audio;    ///< true if the stream has audio data
    enum AVCodecID audio_codec;  ///< codec ID of the audio stream

    union VideoMasterAudioInfo
        audio_info;  ///< audio information for the stream

    uint32_t audio_sample_rate;  ///< sample rate of the audio stream
    uint32_t audio_nb_channels;  ///< number of channels in the audio stream
    uint32_t audio_sample_size;  ///< bits per sample in the audio stream

    AVFormatContext
             *avctx;  ///< AVFormatContext associated with the video stream
    AVStream *video_stream;  ///< AVStream associated with the video stream
    AVStream *audio_stream;  ///< AVStream associated with the audio stream

    // sync data
    int64_t pts;

    uint8_t *video_buffer;       ///< buffer to store the video data
    uint32_t video_buffer_size;  ///< size of the video buffer
    uint32_t frames_received;    ///< number of frames received
    uint32_t frames_dropped;     ///< number of frames dropped

    uint8_t *audio_buffer;           ///< buffer to store the audio data
    uint32_t audio_buffer_size;      ///< size of the audio buffer
    uint32_t audio_frames_received;  ///< number of audio frames received
    uint32_t audio_slots_received;   ///< cumulative number of ST2110-30 audio
                                     ///< slots received (IP audio-only path)
    uint32_t audio_slots_dropped;    ///< cumulative number of ST2110-30 audio
                                     ///< slots dropped (IP audio-only path)

    /* RTP timestamp unwrap state (32-bit -> monotonic 64-bit), per essence:
     * video and audio have independent RTP media clocks. */
    bool     rtp_ts_initialized_video;
    uint32_t rtp_ts_last_raw_video;
    int64_t  rtp_ts_unwrapped_video;
    bool     rtp_ts_initialized_audio;
    uint32_t rtp_ts_last_raw_audio;
    int64_t  rtp_ts_unwrapped_audio;

    /* Hardware timestamp normalization base, per essence (independent
     * hardware clocks, unlike the single board-wide osc/system clock). */
    uint64_t hw_ts_base_video;
    uint64_t hw_ts_base_audio;

    /* Non-sync IP audio capture thread + its packet queue (see
     * videomaster_ip.c). */
    bool                   ip_audio_thread_active;
    pthread_t              ip_audio_thread;
    VideoMasterPacketQueue ip_audio_queue;

} VideoMasterContext;

/**
 * @brief Structure used to store the command line options for the VideoMaster
 * DELTACAST(c) device.
 *
 * This structure holds parameters passed via the FFmpeg command line interface
 * that configure which VideoMaster device to use and how to interact with it.
 * The options are parsed during the initialization phase and used throughout
 * the device's operation.
 */
typedef struct VideoMasterData
{
    AVClass *av_class;  ///< Class for AVOption handling

    void *context;  /// DELTACAST board context

    /* Command Options */
    int64_t board_index;       ///< index of the board to use
    int64_t channel_index;     ///< index of the stream to use
    int64_t timestamp_source;  ///< source of the timestamp
    int64_t nb_channels;       ///< number of channels to use
    int64_t sample_rate;       ///< sample rate of the audio stream
    int64_t sample_size;       ///< bits per sample in the audio stream
    int64_t buffer_packing;    ///< buffer packing format
    int64_t dual_stream;  ///< 0/1 if the stream must be configured with 3G-B-DS
                          ///< interface

    char   *ip_video_destination;
    int64_t ip_video_udp_port;
    char   *ip_video_sps_destination;
    int64_t ip_video_sps_udp_port;
    char   *ip_video_source;
    char   *ip_video_sps_source;
    int64_t ip_video_udp_port_src;
    int64_t ip_video_sps_udp_port_src;
    int64_t ip_video_payload_type;
    int64_t ip_video_sps_payload_type;
    int64_t ip_video_width;
    int64_t ip_video_height;
    int64_t ip_video_framerate_num;
    int64_t ip_video_framerate_den;
    int64_t ip_video_interlaced;
    int64_t ip_video_bit_depth;
    char   *ip_video_sdp_file;

    /* Audio IP ST2110-30 options */
    char   *ip_audio_destination;
    int64_t ip_audio_udp_port;
    char   *ip_audio_source;
    int64_t ip_audio_udp_port_src;
    int64_t ip_audio_payload_type;
    char   *ip_audio_sps_destination;
    int64_t ip_audio_sps_udp_port;
    char   *ip_audio_sps_source;
    int64_t ip_audio_sps_udp_port_src;
    int64_t ip_audio_sps_payload_type;
    char   *ip_audio_sdp_file;
    int64_t ip_audio_nb_channels;
    int64_t ip_audio_packet_time;
    int64_t ip_audio_format;
    int64_t ip_audio_timestamp_source;  ///< pts source for the IP audio
                                        ///< essence; -1 (default) means
                                        ///< "follow timestamp_source"
    int64_t ip_sync;
} VideoMasterData;

/**
 * @brief Closes the handle to the VideoMaster board.
 *
 * This function releases the handle to the VideoMaster board specified in the
 * provided VideoMaster context. It ensures proper cleanup of resources
 * associated with the board handle.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error
 */
int ff_videomaster_close_board_handle(VideoMasterContext *videomaster_context);

/**
 * @brief Closes the stream handle to the VideoMaster stream.
 *
 * This function releases the stream_handle to the VideoMaster stream specified
 * in the provided VideoMaster context. It ensures proper cleanup of resources
 * associated with the stream handle.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error
 */
int ff_videomaster_close_stream_handle(VideoMasterContext *videomaster_context);

/**
 * @brief Inserts a new device information entry into the device info list
 * for a specified board.
 *
 * This function retrieves information about a VideoMaster DELTACAST(c)
 * device identified by the given board index and inserts it into the
 * provided device info list. The function ensures that the device info list
 * is updated with the relevant details about the specified board.
 *
 * @param videomaster_context Pointer to the VideoMaster context containing
 * the board handle and other configurations.
 * @param board_index The index of the board to retrieve information from.
 * @param device_list Pointer to the device info list where the new device
 * information will be added.
 * @return 0 on success, or a negative AVERROR code on failure:
 *         - AVERROR(EIO): Indicates an I/O error occurred while accessing
 * the board.
 *         - AVERROR(ENOMEM): Indicates a memory allocation failure occurred
 * while updating the device list.
 */
int ff_videomaster_create_devices_infos_from_board_index(
    VideoMasterContext *videomaster_context, uint32_t board_index,
    struct AVDeviceInfoList **device_list);

/**
 * @brief Extracts or creates VideoMaster context and command line data.
 *
 * Retrieves or allocates VideoMaster context and command line data structures
 * from the provided AVFormatContext.
 *
 * @param avctx Format context to extract from or store new context in.
 * @param videomaster_data Pointer to receive the command line data.
 * @param videomaster_context Pointer to receive the operational context.
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_videomaster_extract_context(AVFormatContext     *avctx,
                                   VideoMasterData    **videomaster_data,
                                   VideoMasterContext **videomaster_context);

/**
 * @brief Retrieves the API version and the number of boards detected by the
 * VideoMaster DELTACAST(c) API.
 *
 * This function queries the VideoMaster DELTACAST(c) API to determine the API
 * version and the number of boards available. The retrieved information is
 * stored in the `api_version` and `number_of_boards` fields of the provided
 * VideoMaster context.
 *
 * @param videomaster_context Pointer to the VideoMaster context where the API
 * version and board count will be stored.
 * @return 0 on success, or a negative AVERROR code on failure:
 *         - AVERROR(EIO): Indicates an I/O error occurred while accessing the
 * API.
 *         - AVERROR(ENOMEM): Indicates a memory allocation failure occurred
 * during the operation.
 */
int ff_videomaster_get_api_info(VideoMasterContext *videomaster_context);

/**
 * @brief Retrieves audio stream properties from a VideoMaster DELTACAST(c)
 * device.
 *
 * This function queries the specified board and stream to retrieve audio
 * properties such as sample rate, number of channels, bits per sample, codec,
 * and audio format. The retrieved properties are stored in the provided
 * pointers.
 *
 * @param avctx        The AVFormatContext associated with the audio stream.
 * @param board_handle Handle to the VideoMaster board.
 * @param channel_index Index of the audio channel to query.
 * @param buffer_packing The selected buffer packing.
 * @param channel_type Pointer to store the type of the board channel.
 * @param audio_info Pointer to store the audio information structure
 * @param sample_rate Pointer to store the sample rate of the audio stream.
 * @param nb_channels Pointer to store the number of channels in the audio
 * stream.
 * @param sample_size Pointer to store the bits per sample of the audio stream.
 * @param codec Pointer to store the codec ID of the audio stream.
 *
 * @return 0 on success, or a negative AVERROR code on failure.
 */
int ff_videomaster_get_audio_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterBufferPacking buffer_packing,
    enum AVVideoMasterChannelType *channel_type,
    union VideoMasterAudioInfo *audio_info, uint32_t *sample_rate,
    uint32_t *nb_channels, uint32_t *sample_size, enum AVCodecID *codec);

/**
 * @brief Get the channel type from the channel index
 *
 * @param avctx  AVFormatContext pointer to the AVFormatContext
 * @param board_handle  Handle to the board
 * @param channel_index  Index of the channel
 */
enum AVVideoMasterChannelType ff_videomaster_get_channel_type_from_index(
    AVFormatContext *avctx, HANDLE board_handle, int channel_index);

/**
 * @brief Retrieves video and audio data from the VideoMaster device started
 * stream.
 *
 * This function fetches video or audio data from the VideoMaster device and
 * updates the provided VideoMaster context with the retrieved data. To retrieve
 * data, stream must be started.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error
 */
int ff_videomaster_get_data(VideoMasterContext *videomaster_context);

/**
 * @brief Computes an AVPacket duration for a raw PCM audio buffer.
 *
 * @param buf_size Size of the audio buffer, in bytes.
 * @param nb_channels Number of audio channels.
 * @param sample_size Bits per sample.
 * @param sample_rate Sample rate, in Hz.
 * @return Duration in microseconds (at least 1).
 */
int64_t ff_videomaster_fill_audio_packet_duration(uint32_t buf_size,
                                                  uint32_t nb_channels,
                                                  uint32_t sample_size,
                                                  uint32_t sample_rate);

/**
 * @brief Initializes a VideoMasterPacketQueue.
 * @param avctx AVFormatContext used for logging.
 * @param q Queue to initialize.
 * @param max_q_size Maximum queue size, in bytes, before packets get dropped.
 */
void ff_videomaster_packet_queue_init(AVFormatContext        *avctx,
                                      VideoMasterPacketQueue *q,
                                      int64_t                 max_q_size);

/**
 * @brief Flushes and destroys a VideoMasterPacketQueue. Safe to call even if
 * a producer might still (briefly) reference it — callers must have already
 * stopped/joined the producer thread first.
 */
void ff_videomaster_packet_queue_end(VideoMasterPacketQueue *q);

/**
 * @brief Requests any blocked ff_videomaster_packet_queue_get() call to
 * return, and marks the queue as aborted so producers can stop cleanly.
 */
void ff_videomaster_packet_queue_abort(VideoMasterPacketQueue *q);

/**
 * @brief Appends a packet to the queue, taking ownership of its reference.
 * @return 0 on success; a negative value if the queue is full (the packet is
 * unreferenced and a warning is logged) or on allocation failure.
 */
int ff_videomaster_packet_queue_put(VideoMasterPacketQueue *q, AVPacket *pkt);

/**
 * @brief Removes and returns the oldest packet in the queue.
 * @param block If non-zero, wait until a packet is available or the queue is
 * aborted; if zero, return AVERROR(EAGAIN) immediately when empty.
 * @return 0 on success, AVERROR(EAGAIN) if empty and non-blocking (or
 * aborted), negative AVERROR otherwise.
 */
int ff_videomaster_packet_queue_get(VideoMasterPacketQueue *q, AVPacket *pkt,
                                    int block);

/**
 * @brief Rejects IP video parameters for non-IP channel types (SDI, HDMI).
 *
 * Returns AVERROR(EINVAL) if any ip_video_* option has been set. Phase 4 (bis)
 * will extend this to cover audio IP parameters once Phase 3 introduces them.
 */
int ff_videomaster_reject_ip_params(VideoMasterData    *data,
                                    VideoMasterContext *ctx);

/**
 * @brief Retrieves the number of available RX channels for a specified board.
 *
 * This function queries the VideoMaster DELTACAST(c) device to determine the
 * number of available RX (receive) channels for the board identified by the
 * `board_handle` stored in the provided VideoMaster context. The result is
 * stored in the `nb_rx_channels` field of the context.
 *
 * @param videomaster_context Pointer to the VideoMaster context containing the
 * board handle and other configurations.
 * @return 0 on success, or a negative AVERROR code on failure:
 *         - AVERROR(EIO): Indicates an I/O error occurred while accessing the
 * board.
 *         - AVERROR(ENOMEM): Indicates a memory allocation failure occurred
 * during the operation.
 */
int ff_videomaster_get_nb_rx_channels(VideoMasterContext *videomaster_context);

/**
 * @brief Retrieves frame statistics from the VideoMaster device.
 *
 * Updates the videomaster_context with the number of video frames received
 * (frames_received) and dropped (frames_dropped) from the board identified
 * by the board_handle stored in the videomaster_context structure.
 *
 * @param videomaster_context Pointer to the VideoMaster context to update with
 * frame statistics
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error
 */
int ff_videomaster_get_slots_counter(VideoMasterContext *videomaster_context);

/**
 * @brief Retrieves audio slot statistics from the VideoMaster device.
 *
 * Updates the videomaster_context with the number of ST2110-30 audio slots
 * received (audio_slots_received) and dropped (audio_slots_dropped),
 * cumulative since the audio stream was started, queried from the
 * ip_audio_stream_handle stored in the videomaster_context structure.
 *
 * @param videomaster_context Pointer to the VideoMaster context to update with
 * audio slot statistics
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error
 */
int ff_videomaster_get_audio_slots_counter(
    VideoMasterContext *videomaster_context);

/**
 * @brief Retrieves the number of available TX channels for a specified board.
 *
 * This function queries the VideoMaster DELTACAST(c) device to determine the
 * number of available TX (transceive) channels for the board identified by the
 * `board_handle` stored in the provided VideoMaster context. The result is
 * stored in the `nb_tx_channels` field of the context.
 *
 * @param videomaster_context Pointer to the VideoMaster context containing the
 * board handle and other configurations.
 * @return 0 on success, or a negative AVERROR code on failure:
 *         - AVERROR(EIO): Indicates an I/O error occurred while accessing the
 * board.
 *         - AVERROR(ENOMEM): Indicates a memory allocation failure occurred
 * during the operation.
 */
int ff_videomaster_get_nb_tx_channels(VideoMasterContext *videomaster_context);

/**
 * @brief Retrieves the current timestamp from the VideoMaster device.
 *
 * This function retrieves the current timestamp from the VideoMaster device
 * using the specified context. The timestamp is stored in the provided
 * timestamp variable, in microseconds, relative to the first timestamp
 * fetched for that slot/essence.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @param slot_handle Handle of the locked slot to read the timestamp from
 * (the video slot, the audio-only slot, etc. — whichever slot was just
 * locked by the caller).
 * @param source Which timestamp source to use for this call (osc/system,
 * hardware, LTC on-board/companion card, or RTP — IP only for RTP). Callers
 * pass the source resolved for the essence being read (video's
 * timestamp_source, or audio's audio_timestamp_source), since IP allows the
 * two essences to use different sources.
 * @param timestamp Pointer to store the retrieved timestamp.
 * @return 0 on success, or negative AVERROR code on failure:
 */
int ff_videomaster_get_timestamp(VideoMasterContext *videomaster_context,
                                 void               *slot_handle,
                                 enum AVVideoMasterTimeStampType source,
                                 uint64_t                       *timestamp);

/**
 * @brief Retrieves video stream properties from a VideoMaster DELTACAST(c)
 * device.
 *
 * This function queries the specified board and stream index to retrieve video
 * properties such as resolution, frame rate, pixel clock, interlacing, color
 * space, and cable bit sampling. The retrieved properties are stored in the
 * provided pointers.
 *
 * @param avctx        The AVFormatContext associated with the video stream.
 * @param board_handle Handle to the VideoMaster board.
 * @param stream_handle Handle to the VideoMaster stream.
 * @param channel_index Index of the stream to query.
 * @param channel_type Pointer to store the type of the board channel.
 * @param video_info Pointer to store the video information structure
 * @param width Pointer to store the width of the video stream.
 * @param height Pointer to store the height of the video stream.
 * @param frame_rate_num Pointer to store the numerator of the frame rate of the
 * video stream.
 * @param frame_rate_den Pointer to store the denominator of the frame rate of
 * the video stream.
 * @param interlaced Pointer to store whether the video stream is interlaced.
 * @param dual_stream Indicates whether the stream must be configured with
 * 3G-B-DS interface instead of using auto-detection.
 *
 * @return 0 on success, or a negative AVERROR code on failure.
 */
int ff_videomaster_get_video_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterChannelType *channel_type,
    union VideoMasterVideoInfo *video_info, uint32_t *width, uint32_t *height,
    uint32_t *frame_rate_num, uint32_t *frame_rate_den, bool *interlaced,
    bool dual_stream);

/**
 * @brief Checks if 3G-B-DS interface is supported on the VideoMaster device.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return true if 3G-B-DS interface is supported, false otherwise.
 */
bool ff_videomaster_is_3g_b_ds_interface_supported(
    VideoMasterContext *videomaster_context);

/**
 * @brief Checks if the channel is locked on the VideoMaster device.
 *
 * Determines whether the specified channel of the board board_handle identified
 * by channel_index in the VideoMaster context is currently locked on a stream
 * and available for use.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return true if the channel is locked, false otherwise.
 */
bool ff_videomaster_is_channel_locked(VideoMasterContext *videomaster_context);

/**
 * @brief Checks if hardware timestamping is supported on the VideoMaster
 * device.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return true if hardware timestamping is supported, false otherwise.
 */
bool ff_videomaster_is_hardware_timestamp_supported(
    VideoMasterContext *videomaster_context);

/**
 * @brief Checks if an LTC companion card is present alongside the VideoMaster
 * device.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return true if ltc companion card is present, false otherwise.
 */
bool ff_videomaster_is_ltc_companion_card_present(
    VideoMasterContext *videomaster_context);

/**
 * @brief Checks if ltc companion card feature is supported by the VideoMaster
 * device.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return true if ltc companion card feature is supported, false otherwise.
 */
bool ff_videomaster_is_ltc_companion_card_supported(
    VideoMasterContext *videomaster_context);

/**
 * @brief Checks if ltc on board timestamping is supported on the VideoMaster
 * device.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return true if ltc on board timestamping is supported, false otherwise.
 */
bool ff_videomaster_is_ltc_on_board_timestamp_supported(
    VideoMasterContext *videomaster_context);

/**
 * @brief Checks if the board supports Precision Time Protocol (PTP).
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return true if PTP is supported, false otherwise.
 */
bool ff_videomaster_is_ptp_supported(VideoMasterContext *videomaster_context);

/**
 * @brief Checks if the board's PTP client is currently locked to a master
 * clock.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return true if PTP is locked, false otherwise (including on error, e.g.
 * PTP not supported).
 */
bool ff_videomaster_is_ptp_locked(VideoMasterContext *videomaster_context);

/**
 * @brief Opens a handle to the VideoMaster board.
 *
 * This function initializes and opens a handle to the VideoMaster board
 * specified by the board index board_index in the provided VideoMaster
 * context. The handle is used for subsequent operations on the board.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error,
 *         AVERROR(ENOMEM) for memory allocation failure
 */
int ff_videomaster_open_board_handle(VideoMasterContext *videomaster_context);

/**
 * @brief Opens a stream handle to the VideoMaster stream.
 *
 * This function initializes and opens a handle stream_handle to the VideoMaster
 * stream specified by board_handle and channel_index in the provided
 * VideoMaster context. The handle is used for subsequent operations on the
 * stream.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error,
 *         AVERROR(ENOMEM) for memory allocation failure
 */
int ff_videomaster_open_stream_handle(VideoMasterContext *videomaster_context);

/**
 * @brief Releases data retrieved from the VideoMaster device.
 *
 * This function releases buffers and resources associated with data previously
 * retrieved from the VideoMaster device.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error
 */
int ff_videomaster_release_data(VideoMasterContext *videomaster_context);

/**
 * @brief Converts VideoMaster sample rate enum to string.
 *
 * Translates AVVideoMasterSampleRateValue enum values to human-readable
 * strings.
 * @param sample_rate VideoMaster sample rate enum value.
 * @return String representation of the sample rate, or "Unknown" if invalid.
 */
const char *ff_videomaster_sample_rate_to_string(
    enum AVVideoMasterSampleRateValue sample_rate);

/**
 * @brief Converts VideoMaster sample size enum to string.
 *
 * Translates AVVideoMasterSampleSizeValue enum values to human-readable
 * strings.
 * @param sample_size VideoMaster sample size enum value.
 * @return String representation of the sample size, or "Unknown" if invalid.
 */
const char *ff_videomaster_sample_size_to_string(
    enum AVVideoMasterSampleSizeValue sample_size);

/**
 * @brief Starts the VideoMaster stream.
 *
 * This function begins receiving data from the stream identified by
 * stream_handle from the VideoMaster device using the specified context. It
 * prepares the device for data transmission.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error
 */
int ff_videomaster_start_stream(VideoMasterContext *videomaster_context);

/**
 * @brief Stops the VideoMaster stream.
 *
 * This function close the started stream data from the VideoMaster device using
 * the specified context. It ensures proper termination of the stream.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @return 0 on success, or negative AVERROR code on failure:
 *         AVERROR(EIO) for I/O error
 */
int ff_videomaster_stop_stream(VideoMasterContext *videomaster_context);

/**
 * @brief Converts VideoMaster timestamp type enum to string.
 *
 * Translates AVVideoMasterTimeStampType enum values to human-readable strings.
 *
 * @param timestamp_type VideoMaster timestamp type enum value.
 * @return String representation of the timestamp type, or "Unknown" if
 * invalid.
 */
const char *ff_videomaster_timestamp_type_to_string(
    enum AVVideoMasterTimeStampType timestamp_type);

#endif /* AVDEVICE_VIDEOMASTER_COMMON_H */
