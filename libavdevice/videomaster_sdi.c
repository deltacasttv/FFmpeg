/**
 * @file videomaster_sdi.c
 * @brief SDI/ASI-specific implementation for the VideoMaster DELTACAST(c)
 *        input device.
 *
 * This file contains both stream setup and demuxer enumeration.
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

#include "videomaster_sdi.h"
#include "videomaster_internal.h"

#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"

#include <string.h>

#if defined(__APPLE__)
#include <VideoMasterHD/VideoMasterHD_Core.h>
#include <VideoMasterHD/VideoMasterHD_String.h>
#else
#include <VideoMasterHD_Core.h>
#include <VideoMasterHD_String.h>
#endif

/* Forward declared from videomaster_common.c */
extern int
ff_videomaster_open_stream_handle(VideoMasterContext *videomaster_context);
extern int ff_videomaster_get_video_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterChannelType *channel_type,
    union VideoMasterVideoInfo *video_info, uint32_t *video_width,
    uint32_t *video_height, uint32_t *video_frame_rate_num,
    uint32_t *video_frame_rate_den, bool *video_interlaced, bool dual_stream);
extern int ff_videomaster_get_audio_stream_properties(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, enum AVVideoMasterBufferPacking buffer_packing,
    enum AVVideoMasterChannelType *channel_type,
    union VideoMasterAudioInfo *audio_info, uint32_t *sample_rate,
    uint32_t *nb_channels, uint32_t *sample_size, enum AVCodecID *codec);
extern VHD_STREAMTYPE
ff_videomaster_get_rx_stream_type_from_index(uint32_t index);

static int get_rx_sdi_board_property_clock_divisor_from_index(uint32_t index)
{
    switch (index)
    {
    case 0:
        return VHD_SDI_BP_RX0_CLOCK_DIV;
    case 1:
        return VHD_SDI_BP_RX1_CLOCK_DIV;
    case 2:
        return VHD_SDI_BP_RX2_CLOCK_DIV;
    case 3:
        return VHD_SDI_BP_RX3_CLOCK_DIV;
    case 4:
        return VHD_SDI_BP_RX4_CLOCK_DIV;
    case 5:
        return VHD_SDI_BP_RX5_CLOCK_DIV;
    case 6:
        return VHD_SDI_BP_RX6_CLOCK_DIV;
    case 7:
        return VHD_SDI_BP_RX7_CLOCK_DIV;
    case 8:
        return VHD_SDI_BP_RX8_CLOCK_DIV;
    case 9:
        return VHD_SDI_BP_RX9_CLOCK_DIV;
    case 10:
        return VHD_SDI_BP_RX10_CLOCK_DIV;
    case 11:
        return VHD_SDI_BP_RX11_CLOCK_DIV;
    default:
        return -1;
    }
}

/* ---- Demuxer validation functions ---- */

int ff_videomaster_validate_arguments_sdi(
    VideoMasterData *videomaster_data, VideoMasterContext *videomaster_context)
{
    /* Dual-stream is SDI-only; validate capability */
    if (videomaster_context->dual_stream &&
        !ff_videomaster_is_3g_b_ds_interface_supported(videomaster_context))
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "3G-B DS interface is not supported on this device and for this "
               "channel. Dual-stream mode cannot be enabled.\n");
        return AVERROR(EINVAL);
    }
    else if (videomaster_context->dual_stream)
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "3G-B Dual-Stream interface enabled\n");
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "3G-B Dual-Stream interface disabled\n");
    }

    /* IP arguments must not be specified for SDI */
    if (videomaster_data->ip_destination != NULL ||
        videomaster_data->ip_udp_port > 0 ||
        videomaster_data->ip_video_payload_type > 0 ||
        videomaster_data->ip_video_width > 0 ||
        videomaster_data->ip_video_height > 0)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "IP-specific arguments (ip_destination, ip_udp_port, "
               "ip_video_payload_type, ip_video_*) are not applicable for SDI "
               "channels.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

int ff_videomaster_check_audio_properties_sdi(
    VideoMasterContext *videomaster_context)
{
    if (videomaster_context->audio_nb_channels == -1 ||
        videomaster_context->audio_sample_rate ==
            AV_VIDEOMASTER_SAMPLE_RATE_UNKNOWN ||
        videomaster_context->audio_sample_size ==
            AV_VIDEOMASTER_SAMPLE_SIZE_UNKNOWN)
    {
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Invalid audio properties: "
               "audio_nb_channels=%d, audio_sample_rate=%s, "
               "audio_sample_size=%s. Audio will be ignored if audio "
               "stream is present.\n",
               (int)videomaster_context->audio_nb_channels,
               ff_videomaster_sample_rate_to_string(
                   videomaster_context->audio_sample_rate),
               ff_videomaster_sample_size_to_string(
                   videomaster_context->audio_sample_size));
    }
    return 0;
}

int ff_videomaster_check_channel_integrity_sdi(
    VideoMasterContext *videomaster_context)
{
    float frame_rate = (float)videomaster_context->video_frame_rate_num /
                       videomaster_context->video_frame_rate_den;

    av_log(videomaster_context->avctx, AV_LOG_TRACE,
           "Channel index is valid\n");

    if (ff_videomaster_get_video_stream_properties(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->stream_handle,
            videomaster_context->channel_index,
            &videomaster_context->channel_type,
            &videomaster_context->video_info, &videomaster_context->video_width,
            &videomaster_context->video_height,
            &videomaster_context->video_frame_rate_num,
            &videomaster_context->video_frame_rate_den,
            &videomaster_context->video_interlaced,
            videomaster_context->dual_stream) == 0)
    {
        videomaster_context->has_video = true;

        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Stream properties: %ux%u@%.3f %s %s\n",
               videomaster_context->video_width,
               videomaster_context->video_height, frame_rate,
               VHD_VIDEOSTANDARD_ToPrettyString(
                   videomaster_context->video_info.sdi.video_standard),
               VHD_CLOCKDIVISOR_ToPrettyString(
                   videomaster_context->video_info.sdi.clock_divisor));
        av_log(videomaster_context->avctx, AV_LOG_TRACE, "Interface: %s\n",
               VHD_INTERFACE_ToPrettyString(
                   videomaster_context->video_info.sdi.interface));

        if (ff_videomaster_open_stream_handle(videomaster_context) != 0)
        {
            av_log(videomaster_context->avctx, AV_LOG_ERROR,
                   "Failed to open stream handle.\n");
            return AVERROR(EIO);
        }
        av_log(videomaster_context->avctx, AV_LOG_TRACE,
               "Stream handle opened successfully\n");
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to get stream properties\n");
        return AVERROR(EIO);
    }

    if (ff_videomaster_get_audio_stream_properties(
            videomaster_context->avctx, videomaster_context->board_handle,
            videomaster_context->stream_handle,
            videomaster_context->channel_index,
            videomaster_context->video_buffer_packing,
            &videomaster_context->channel_type,
            &videomaster_context->audio_info,
            &videomaster_context->audio_sample_rate,
            &videomaster_context->audio_nb_channels,
            &videomaster_context->audio_sample_size,
            &videomaster_context->audio_codec) == 0)
    {
        if (videomaster_context->audio_sample_size !=
                AV_VIDEOMASTER_SAMPLE_SIZE_UNKNOWN &&
            videomaster_context->audio_sample_rate !=
                AV_VIDEOMASTER_SAMPLE_RATE_UNKNOWN &&
            videomaster_context->audio_nb_channels != 0)
        {
            videomaster_context->has_audio = true;
            av_log(videomaster_context->avctx, AV_LOG_TRACE,
                   "Audio properties: %u channels @%uHz (%u bits)\n",
                   videomaster_context->audio_nb_channels,
                   videomaster_context->audio_sample_rate,
                   videomaster_context->audio_sample_size);
        }
        else
        {
            av_log(videomaster_context->avctx, AV_LOG_WARNING,
                   "Audio properties: No audio detected\n");
        }
    }
    else
    {
        av_log(videomaster_context->avctx, AV_LOG_WARNING,
               "Failed to get audio properties\n");
    }

    return 0;
}

bool ff_videomaster_is_channel_locked_sdi(
    VideoMasterContext *videomaster_context)
{
    uint32_t channel_status = 0;

    if (ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_GetChannelProperty(videomaster_context->board_handle,
                                   VHD_RX_CHANNEL,
                                   videomaster_context->channel_index,
                                   VHD_CORE_CP_STATUS, &channel_status),
            "Channel status retrieved successfully",
            "Failed to retrieve channel status") != 0)
        return false;

    return !(channel_status & VHD_CORE_RXSTS_UNLOCKED);
}

void ff_videomaster_format_channel_description_sdi(
    VideoMasterContext *videomaster_context, const char *board_name,
    const char *serial_number, char *buf, size_t buf_size)
{
    double      frame_rate = (double)videomaster_context->video_frame_rate_num /
                             videomaster_context->video_frame_rate_den;
    const char *interface_str = VHD_INTERFACE_ToPrettyString(
        videomaster_context->video_info.sdi.interface);

    snprintf(buf, buf_size,
             "SDI video: %ux%u%s%.3f (interface: %s) on board %s (SN: %s)",
             videomaster_context->video_width,
             videomaster_context->video_height,
             videomaster_context->video_interlaced ? "i" : "p",
             videomaster_context->video_interlaced ? frame_rate * 2
                                                   : frame_rate,
             interface_str, board_name, serial_number);
}

int ff_videomaster_sdi_init_audio_info(VideoMasterContext *videomaster_context,
                                       VHD_AUDIOINFO      *audio_info)
{
    VHD_AUDIOGROUP   *audio_group = NULL;
    VHD_AUDIOCHANNEL *audio_channel = NULL;
    VHD_AUDIOFORMAT   buffer_format = videomaster_context->audio_sample_size ==
                                              AV_VIDEOMASTER_SAMPLE_SIZE_16
                                          ? VHD_AF_16
                                          : VHD_AF_24;
    uint32_t          channel_count = 0;
    uint32_t          nb_samples = VHD_GetNbSamples(
        videomaster_context->video_info.sdi.video_standard,
        videomaster_context->video_info.sdi.clock_divisor, VHD_ASR_48000, 0);
    uint32_t nb_channels = videomaster_context->audio_nb_channels;

    memset(audio_info, 0, sizeof(VHD_AUDIOINFO));

    for (int audio_group_idx = 0;
         audio_group_idx < VHD_NBOFGROUP && channel_count < nb_channels;
         audio_group_idx++)
    {
        for (int channel_idx = 0;
             channel_idx < VHD_NBOFCHNPERGROUP && channel_count < nb_channels;
             channel_idx++, channel_count++)
        {
            audio_group = &audio_info->pAudioGroups[audio_group_idx];
            audio_channel = &audio_group->pAudioChannels[channel_idx];

            audio_channel->Mode = (nb_channels - channel_count) <= 1 &&
                                          (nb_channels % 2 == 1)
                                      ? VHD_AM_MONO
                                      : VHD_AM_STEREO;
            audio_channel->BufferFormat = buffer_format;
            if (channel_idx % 2 == 0)
            {
                audio_channel->DataSize =
                    nb_samples * VHD_GetBlockSize(audio_channel->BufferFormat,
                                                  audio_channel->Mode);
                audio_channel->pData = av_malloc(audio_channel->DataSize);
                if (!audio_channel->pData)
                {
                    av_log(videomaster_context->avctx, AV_LOG_ERROR,
                           "Failed to allocate memory for audio channel "
                           "%d in group %d\n",
                           channel_idx, audio_group_idx);
                    return AVERROR(ENOMEM);
                }
            }
        }
    }

    return 0;
}

int ff_videomaster_release_audio_info_sdi(
    VideoMasterContext *videomaster_context, VHD_AUDIOINFO *audio_info)
{
    for (int audio_group_idx = 0; audio_group_idx < VHD_NBOFGROUP;
         audio_group_idx++)
        for (int channel_index = 0; channel_index < VHD_NBOFCHNPERGROUP;
             channel_index++)
        {
            VHD_AUDIOCHANNEL *audio_channel =
                &audio_info->pAudioGroups[audio_group_idx]
                     .pAudioChannels[channel_index];
            if (audio_channel->pData)
            {
                av_log(videomaster_context->avctx, AV_LOG_TRACE,
                       "Freeing audio data buffer of size %u\n",
                       audio_channel->DataSize);
                av_freep(&audio_channel->pData);
                audio_channel->pData = NULL;
            }
        }

    return 0;
}

static int interleaved_audio_info_to_audio_buffer_sdi(
    VideoMasterContext *videomaster_context, VHD_AUDIOINFO *audio_info,
    uint8_t **audio_buffer, uint32_t *audio_buffer_size)
{
    uint8_t *channel_buffers[VHD_NBOFGROUP * VHD_NBOFCHNPERGROUP / 2] = { 0 };
    uint32_t channel_sizes[VHD_NBOFGROUP * VHD_NBOFCHNPERGROUP / 2] = { 0 };
    VHD_AUDIOMODE channel_modes[VHD_NBOFGROUP * VHD_NBOFCHNPERGROUP / 2] = {
        0
    };
    uint32_t nb_channels = videomaster_context->audio_nb_channels;
    uint32_t bytes_per_sample = videomaster_context->audio_sample_size / 8;
    uint32_t nb_samples = 0;
    uint32_t channel_pair_count = 0;
    uint32_t samples_in_channel = 0;
    uint32_t dst_offset = 0;
    uint32_t src_offset = 0;
    uint32_t channel_pair_index = 0;
    uint8_t *src = NULL;
    uint8_t *dst_ptr = NULL;
    uint8_t *src_ptr = NULL;
    uint32_t mode = 0;
    uint32_t size = 0;

    if (audio_info == NULL || audio_buffer == NULL || audio_buffer_size == NULL)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Invalid parameters for interleaved audio info to audio buffer "
               "conversion\n");
        return AVERROR(EINVAL);
    }

    for (int group = 0;
         group < VHD_NBOFGROUP && channel_pair_count < nb_channels; group++)
    {
        for (int even_channel_idx = 0;
             even_channel_idx < VHD_NBOFCHNPERGROUP / 2 &&
             channel_pair_count < nb_channels;
             even_channel_idx++, channel_pair_count++)
        {
            int               channel_index = even_channel_idx * 2;
            VHD_AUDIOCHANNEL *audio_channel =
                &audio_info->pAudioGroups[group].pAudioChannels[channel_index];
            channel_buffers[channel_pair_count] = audio_channel->pData;
            channel_sizes[channel_pair_count] = audio_channel->DataSize;
            channel_modes[channel_pair_count] = audio_channel->Mode;
            samples_in_channel = 0;
            if (audio_channel->Mode == VHD_AM_STEREO)
                samples_in_channel = audio_channel->DataSize /
                                     (2 * bytes_per_sample);
            else
                samples_in_channel = audio_channel->DataSize / bytes_per_sample;
            if (samples_in_channel > nb_samples)
                nb_samples = samples_in_channel;
        }
    }

    *audio_buffer_size = nb_samples * nb_channels * bytes_per_sample;
    *audio_buffer = av_mallocz(*audio_buffer_size);
    if (!*audio_buffer)
    {
        av_log(videomaster_context->avctx, AV_LOG_ERROR,
               "Failed to allocate memory for audio buffer\n");
        return AVERROR(ENOMEM);
    }

    for (uint32_t sample_index = 0; sample_index < nb_samples; sample_index++)
    {
        dst_offset = sample_index * nb_channels * bytes_per_sample;
        channel_pair_index = 0;
        for (uint32_t channel_index = 0; channel_index < nb_channels;)
        {
            src = channel_buffers[channel_pair_index];
            mode = channel_modes[channel_pair_index];
            size = channel_sizes[channel_pair_index];
            if (mode == VHD_AM_STEREO)
            {
                for (int lr_channel = 0;
                     lr_channel < 2 && channel_index < nb_channels;
                     lr_channel++, channel_index++)
                {
                    src_offset = (sample_index * 2 + lr_channel) *
                                 bytes_per_sample;
                    src_ptr = NULL;
                    if (src && src_offset + bytes_per_sample <= size)
                        src_ptr = src + src_offset;
                    dst_ptr = (uint8_t *)*audio_buffer + dst_offset +
                              channel_index * bytes_per_sample;
                    if (src_ptr)
                        memcpy(dst_ptr, src_ptr, bytes_per_sample);
                }
            }
            else
            {
                src_offset = sample_index * bytes_per_sample;
                src_ptr = NULL;
                if (src && src_offset + bytes_per_sample <= size)
                    src_ptr = src + src_offset;
                dst_ptr = (uint8_t *)*audio_buffer + dst_offset +
                          channel_index * bytes_per_sample;
                if (src_ptr)
                    memcpy(dst_ptr, src_ptr, bytes_per_sample);
                channel_index++;
            }
            channel_pair_index++;
        }
    }

    return 0;
}

int ff_videomaster_get_audio_buffer_sdi(VideoMasterContext *videomaster_context)
{
    VHD_AUDIOINFO *audio_info = &videomaster_context->audio_info.sdi.audio_info;

    if (ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_SlotExtractAudio(videomaster_context->slot_handle, audio_info),
            "Audio slot buffer retrieved successfully",
            "Failed to retrieve audio slot buffer") != 0)
        return AVERROR(EIO);

    return interleaved_audio_info_to_audio_buffer_sdi(
        videomaster_context, audio_info, &videomaster_context->audio_buffer,
        &videomaster_context->audio_buffer_size);
}

uint32_t ff_videomaster_get_stream_proc_sdi(void)
{
    return VHD_SDI_STPROC_JOINED;
}

uint32_t ff_videomaster_get_video_buffer_type_sdi(void)
{
    return VHD_SDI_BT_VIDEO;
}

int ff_videomaster_get_video_stream_properties_sdi(
    AVFormatContext *avctx, HANDLE board_handle, HANDLE stream_handle,
    uint32_t channel_index, union VideoMasterVideoInfo *video_info,
    uint32_t *width, uint32_t *height, uint32_t *frame_rate_num,
    uint32_t *frame_rate_den, bool *interlaced, bool dual_stream)
{
    uint32_t frame_rate = 0;
    HANDLE   local_stream_handle = stream_handle;
    int      av_status = 0;
    BOOL32   interlaced_tmp = 0;

    int board_property_clock_divisor =
        get_rx_sdi_board_property_clock_divisor_from_index(channel_index);
    VHD_STREAMTYPE stream_type = ff_videomaster_get_rx_stream_type_from_index(
        channel_index);

    if (board_property_clock_divisor == -1)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Unsupported channel index %u for SDI "
               "clock divisor board property\n",
               channel_index);
        return AVERROR(EINVAL);
    }

    if (stream_type == NB_VHD_STREAMTYPES)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Unsupported channel index %u for SDI stream type\n",
               channel_index);
        return AVERROR(EINVAL);
    }

    video_info->sdi.video_standard = NB_VHD_VIDEOSTANDARDS;
    ff_videomaster_handle_vhd_status(
        avctx,
        VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL, channel_index,
                               VHD_SDI_CP_VIDEO_STANDARD,
                               (uint32_t *)&video_info->sdi.video_standard),
        "", "");

    if (video_info->sdi.video_standard == NB_VHD_VIDEOSTANDARDS)
    {
        if (!dual_stream)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot auto-detect the video standard for the SDI stream. "
                   "If this is the chosen interface for the input stream, "
                   "consider enabling dual-stream mode (3G_B_DS_425_1 "
                   "interface). If this does not resolve the issue, please "
                   "contact DELTACAST.TV support for further assistance.\n");
            return AVERROR(EIO);
        }

        video_info->sdi.interface = VHD_INTERFACE_3G_B_DS_425_1;

        if (local_stream_handle == NULL)
        {
            if ((av_status = ff_videomaster_handle_vhd_status(
                     avctx,
                     VHD_OpenStreamHandle(board_handle, stream_type,
                                          ff_videomaster_get_stream_proc_sdi(),
                                          NULL, &local_stream_handle, NULL),
                     "Stream handle opened successfully",
                     "Failed to open stream handle")) != 0)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "Failed to open stream handle for SDI channel %u in "
                       "dual stream mode to get video properties\n",
                       channel_index);
                return av_status;
            }
        }

        if ((av_status = ff_videomaster_handle_vhd_status(
                 avctx,
                 VHD_SetStreamProperty(local_stream_handle,
                                       VHD_SDI_SP_INTERFACE,
                                       video_info->sdi.interface),
                 "",
                 "Failed to set VHD_INTERFACE_3G_B_DS_425_1 interface on "
                 "stream handle")) != 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to set stream interface to 3G_B_DS_425_1 "
                   "for SDI channel %u\n",
                   channel_index);
            if (stream_handle == NULL)
                VHD_CloseStreamHandle(local_stream_handle);
            return av_status;
        }

        if ((av_status = ff_videomaster_handle_vhd_status(
                 avctx, VHD_StartStream(local_stream_handle), "",
                 "Failed to start stream to detect video properties")) != 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to start stream for SDI channel %u in dual "
                   "stream mode to get video properties\n",
                   channel_index);
            if (stream_handle == NULL)
                VHD_CloseStreamHandle(local_stream_handle);
            return av_status;
        }

        if ((av_status = ff_videomaster_handle_vhd_status(
                 avctx,
                 VHD_GetStreamProperty(
                     local_stream_handle, VHD_SDI_SP_VIDEO_STANDARD,
                     (uint32_t *)&video_info->sdi.video_standard),
                 "",
                 "Failed to get SDI video standard from stream "
                 "properties")) != 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to get SDI video standard from stream properties "
                   "for SDI channel %u in dual stream mode\n",
                   channel_index);
            VHD_StopStream(local_stream_handle);
            if (stream_handle == NULL)
                VHD_CloseStreamHandle(local_stream_handle);
            return av_status;
        }

        if (video_info->sdi.video_standard == NB_VHD_VIDEOSTANDARDS)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Video standard could not be detected from a dual "
                   "stream\n");
            VHD_StopStream(local_stream_handle);
            if (stream_handle == NULL)
                VHD_CloseStreamHandle(local_stream_handle);
            return AVERROR(EIO);
        }

        if ((av_status = ff_videomaster_handle_vhd_status(
                 avctx,
                 VHD_GetBoardProperty(
                     board_handle, board_property_clock_divisor,
                     (uint32_t *)&video_info->sdi.clock_divisor),
                 "", "")) != 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to get SDI clock divisor from board properties for "
                   "SDI channel %u\n",
                   channel_index);
            VHD_StopStream(local_stream_handle);
            if (stream_handle == NULL)
                VHD_CloseStreamHandle(local_stream_handle);
            return av_status;
        }

        if (video_info->sdi.clock_divisor == NB_VHD_CLOCKDIVISORS)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Unsupported clock divisor retrieved from board properties "
                   "for SDI channel %u\n",
                   channel_index);
            VHD_StopStream(local_stream_handle);
            if (stream_handle == NULL)
                VHD_CloseStreamHandle(local_stream_handle);
            return AVERROR(EIO);
        }

        if ((av_status = ff_videomaster_handle_vhd_status(
                 avctx, VHD_StopStream(local_stream_handle), "",
                 "Failed to stop stream")) != 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to stop stream for SDI channel %u in dual stream "
                   "mode\n",
                   channel_index);
            if (stream_handle == NULL)
                VHD_CloseStreamHandle(local_stream_handle);
            return av_status;
        }

        if (stream_handle == NULL)
        {
            if ((av_status = ff_videomaster_handle_vhd_status(
                     avctx, VHD_CloseStreamHandle(local_stream_handle),
                     "Stream handle closed successfully",
                     "Failed to close stream handle")) != 0)
            {
                av_log(avctx, AV_LOG_ERROR,
                       "Failed to close stream handle for SDI channel %u in "
                       "dual stream mode\n",
                       channel_index);
                return av_status;
            }
        }
    }
    else
    {
        if ((av_status = ff_videomaster_handle_vhd_status(
                 avctx,
                 VHD_GetChannelProperty(board_handle, VHD_RX_CHANNEL,
                                        channel_index, VHD_SDI_CP_INTERFACE,
                                        (uint32_t *)&video_info->sdi.interface),
                 "", "")) != 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to get SDI interface from channel properties for "
                   "SDI channel %u\n",
                   channel_index);
            return av_status;
        }

        if ((av_status = ff_videomaster_handle_vhd_status(
                 avctx,
                 VHD_GetChannelProperty(
                     board_handle, VHD_RX_CHANNEL, channel_index,
                     VHD_SDI_CP_CLOCK_DIVISOR,
                     (uint32_t *)&video_info->sdi.clock_divisor),
                 "", "")) != 0)
        {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to get SDI clock divisor from channel properties "
                   "for SDI channel %u\n",
                   channel_index);
            return av_status;
        }
    }

    if ((av_status = ff_videomaster_handle_vhd_status(
             avctx,
             VHD_GetVideoCharacteristics(video_info->sdi.video_standard, width,
                                         height, &interlaced_tmp, &frame_rate),
             "", "")) != 0)
    {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to get video characteristics for SDI channel %u\n",
               channel_index);
        return av_status;
    }

    *interlaced = !!interlaced_tmp;
    *frame_rate_num = frame_rate * 1000;

    switch (video_info->sdi.clock_divisor)
    {
    case VHD_CLOCKDIV_1:
        *frame_rate_den = 1000;
        break;
    case VHD_CLOCKDIV_1001:
        *frame_rate_den = 1001;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported clock divisor: %u\n",
               video_info->sdi.clock_divisor);
        return AVERROR(EIO);
    }

    return 0;
}

/* ---- Stream setup functions ---- */

int ff_videomaster_start_stream_sdi(VideoMasterContext *videomaster_context)
{
    if (videomaster_context->channel_type == AV_VIDEOMASTER_CHANNEL_ASISDI)
        ff_videomaster_handle_vhd_status(
            videomaster_context->avctx,
            VHD_SetChannelProperty(videomaster_context->board_handle,
                                   VHD_RX_CHANNEL,
                                   videomaster_context->channel_index,
                                   VHD_CORE_CP_MODE, VHD_CHANNEL_MODE_SDI),
            "", "");

    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(
            videomaster_context->stream_handle, VHD_SDI_SP_VIDEO_STANDARD,
            videomaster_context->video_info.sdi.video_standard),
        "", "");
    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(
            videomaster_context->stream_handle, VHD_SDI_BP_GENLOCK_CLOCK_DIV,
            videomaster_context->video_info.sdi.clock_divisor),
        "", "");
    ff_videomaster_handle_vhd_status(
        videomaster_context->avctx,
        VHD_SetStreamProperty(videomaster_context->stream_handle,
                              VHD_SDI_SP_INTERFACE,
                              videomaster_context->video_info.sdi.interface),
        "", "");

    if (videomaster_context->has_audio)
        ff_videomaster_sdi_init_audio_info(
            videomaster_context,
            &videomaster_context->audio_info.sdi.audio_info);

    return 0;
}
