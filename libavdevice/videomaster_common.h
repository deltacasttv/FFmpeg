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
#include "libavdevice/avdevice.h"
#include "libavutil/avutil.h"

#if defined(__APPLE__)
#include <VideoMasterHD/VideoMasterHD_Core.h>
#include <VideoMasterHD/VideoMasterHD_Dv.h>
#include <VideoMasterHD/VideoMasterHD_Dv_Audio.h>
#include <VideoMasterHD/VideoMasterHD_Sdi.h>
#include <VideoMasterHD/VideoMasterHD_Sdi_Audio.h>
#include <VideoMasterHD/VideoMasterHD_String.h>
#else
#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_Dv.h>
#include <VideoMasterHD_Dv_Audio.h>
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
    AV_VIDEOMASTER_TIMESTAMP_OSCILLATOR = VHD_ST_CLK_TYPE_MONOTONIC_RAW,
    AV_VIDEOMASTER_TIMESTAMP_SYSTEM = VHD_ST_CLK_TYPE_REALTIME,
    AV_VIDEOMASTER_TIMESTAMP_HARDWARE = NB_VHD_SYSTEM_TIME_CLK_TYPE,
    AV_VIDEOMASTER_TIMESTAMP_NB
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
        uint32_t genlock_offset;  ///< genlock offset for the video stream
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
        timestamp_source;  ///< source of the timestamp

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
    uint32_t video_frame_rate_den;    ///< denominator for the frame rate of the
                                      ///< video stream
    bool           video_interlaced;  ///< interlaced mode of the video stream
    enum AVCodecID video_codec;       ///< codec ID of the video stream
    enum AVPixelFormat
             video_pixel_format;  ///< pixel format of the video stream
    uint32_t video_bit_rate;      ///< bit rate of the video stream

    bool
        return_video_next;  ///< true if the next video frame should be returned

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
    uint32_t channel_index, enum AVVideoMasterChannelType *,
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
 * timestamp
 * variable. The timestamp source is determined by the timestamp_source
 * field in the VideoMaster context.
 *
 * @param videomaster_context The VideoMaster context to use.
 * @param timestamp Pointer to store the retrieved timestamp.
 * @return 0 on success, or negative AVERROR code on failure:
 */
int ff_videomaster_get_timestamp(VideoMasterContext *videomaster_context,
                                 uint64_t           *timestamp);

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
 *
 * @return 0 on success, or a negative AVERROR code on failure.
 */
int ff_videomaster_get_video_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterChannelType *channel_type,
    union VideoMasterVideoInfo *video_info, uint32_t *width, uint32_t *height,
    uint32_t *frame_rate_num, uint32_t *frame_rate_den, bool *interlaced);

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
bool ff_videomaster_is_hardware_timestamp_is_supported(
    VideoMasterContext *videomaster_context);

/**
 * @brief Opens a handle to the VideoMaster board.
 *
 * This function initializes and opens a handle to the VideoMaster board
 * specified by the board index board_index in the provided VideoMaster context.
 * The handle is used for subsequent operations on the board.
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
