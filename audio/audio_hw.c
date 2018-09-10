/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_primary"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>

//#include "ril_interface.h"
//#include "audio_route.h"
#define PCM_CARD 0
#define PCM_DEVICE 0
#define PCM_DEVICE_SCO 0

#define MIXER_CARD 0

#define OUT_PERIOD_SIZE 1024
#define OUT_SHORT_PERIOD_COUNT 2
#define OUT_LONG_PERIOD_COUNT 4
#define OUT_SAMPLING_RATE 44100

#define IN_PERIOD_SIZE 320
#define IN_PERIOD_COUNT 8
#define IN_SAMPLING_RATE 8000

#define SCO_PERIOD_SIZE 320
#define SCO_PERIOD_COUNT 4
#define SCO_SAMPLING_RATE 8000

#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <fcntl.h>
#define PCG_IOCTL_DEVICE "/dev/bcm_alsa_pcg"
#define PCG_IOCTL_SETMODEAPP 112
#define PCG_IOCTL_SETVOL 103
#define PCG_IOCTL_SETEXTPAVOL 105
#define ALSA_CONTROL_IOCTL "/dev/snd/controlC0"
#define SNDRV_CTL_IOCTL_PCM_PREFER_SUBDEVICE _IOW('U', 0x32, int)

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 2000
#define MAX_WRITE_SLEEP_US ((OUT_PERIOD_SIZE * OUT_SHORT_PERIOD_COUNT * 1000000) \
                                / OUT_SAMPLING_RATE)

enum {
    OUT_BUFFER_TYPE_UNKNOWN,
    OUT_BUFFER_TYPE_SHORT,
    OUT_BUFFER_TYPE_LONG,
};

struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = OUT_SAMPLING_RATE,
    .period_size = OUT_PERIOD_SIZE,
    .period_count = OUT_LONG_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = OUT_PERIOD_SIZE * OUT_SHORT_PERIOD_COUNT,
};

struct pcm_config pcm_config_in = {
    .channels = 1,
    .rate = IN_SAMPLING_RATE,
    .period_size = IN_PERIOD_SIZE,
    .period_count = IN_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 1,
    .stop_threshold = (IN_PERIOD_SIZE * IN_PERIOD_COUNT),
};

struct pcm_config pcm_config_sco = {
    .channels = 1,
    .rate = SCO_SAMPLING_RATE,
    .period_size = SCO_PERIOD_SIZE,
    .period_count = SCO_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

#define MIN(x, y) ((x) > (y) ? (y) : (x))

struct route_setting
{
    char *ctl_name;
    int intval;
    char *strval;
};

/* These are values that never change */
struct route_setting defaults[] = {
    /* general */
    {
        .ctl_name = "Speaker Playback Volume",
        .intval = 0,
    },
    {
        .ctl_name = "Speaker Playback Switch",
        .intval = 0,
    },
    {
        .ctl_name = "Mic Capture Volume",
        .intval = 0,
    },
    {
        .ctl_name = "Mic Capture Switch",
        .intval = 1,
    },
    {
        .ctl_name = "Earpiece Playback Volume",
        .intval = 0,
    },
    {
        .ctl_name = "Earpiece Playback Switch",
        .intval = 0,
    },

    /* headset */
    {
        .ctl_name = "Headset Playback Volume",
        .intval = 0,
    },
    {
        .ctl_name = "Headset Playback Switch",
        .intval = 0,
    },

    /* bt */
    {
        .ctl_name = "BTHeadset Volume",
        .intval = 0,
    },
    {
        .ctl_name = "BTHeadset Switch",
        .intval = 0,
    },
    {
        .ctl_name = "PCM Playback Route",
        .intval = 0,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting earpiece[] = {
    {
        .ctl_name = "Earpiece Playback Volume",
        .intval = 5,
    },
    {
        .ctl_name = "Earpiece Playback Switch",
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting headphone[] = {
    {
        .ctl_name = "Headset Playback Volume",
        .intval = 15,
    },
    {
        .ctl_name = "Headset Playback Switch",
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting speaker[] = {
    {
        .ctl_name = "Speaker Playback Volume",
        .intval = 15,
    },
    {
        .ctl_name = "Speaker Playback Switch",
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting sco[] = {
    {
        .ctl_name = "BTHeadset Volume",
        .intval = 40,
    },
    {
        .ctl_name = "BTHeadset Switch",
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting mic[] = {
    {
        .ctl_name = "Mic Capture Volume",
        .intval = 5,
    },
    {
        .ctl_name = "Mic Capture Switch",
        .intval = 0,
    },
    {
        .ctl_name = NULL,
    },
};

struct route_setting mic_off[] = {
    {
        .ctl_name = "Mic Capture Volume",
        .intval = 0,
    },
    {
        .ctl_name = "Mic Capture Switch",
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

struct mixer_ctls
{
    struct mixer_ctl *speaker_enable;
    struct mixer_ctl *speaker_volume;
    struct mixer_ctl *earpiece_enable;
    struct mixer_ctl *earpiece_volume;
    struct mixer_ctl *headset_enable;
    struct mixer_ctl *headset_volume;
    struct mixer_ctl *mic_enable;
    struct mixer_ctl *mic_volume;
    struct mixer_ctl *sco_enable;
    struct mixer_ctl *sco_volume;
    struct mixer_ctl *pcm_route;
};

struct tuna_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct mixer *mixer;
    struct mixer_ctls mixer_ctls;
    int mode;
    int devices;
    int in_call;
//    float voice_volume;
    struct tuna_stream_in *active_input;
    struct tuna_stream_out *active_output;
    bool mic_mute;
    int orientation;
    bool screen_off;
    int subdevice;
    struct echo_reference_itfe *echo_reference;
};

/*enum pcm_type {
    PCM_NORMAL = 0,
    PCM_SPDIF,
    PCM_HDMI,
    PCM_TOTAL,
};
*/
struct tuna_stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    struct resampler_itfe *resampler;
    char *buffer;
//    int16_t *buffer;
    int standby;
    struct echo_reference_itfe *echo_reference;
    struct tuna_audio_device *dev;
    int write_threshold;
    int buffer_type;
};

#define MAX_PREPROCESSORS 3 /* maximum one AGC + one NS + one AEC per input stream */

struct tuna_stream_in {
    struct audio_stream_in stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    int device;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t frames_in;
    unsigned int requested_rate;
    int standby;
    int source;
    struct echo_reference_itfe *echo_reference;
    bool need_echo_reference;
    effect_handle_t preprocessors[MAX_PREPROCESSORS];
    int num_preprocessors;
    int16_t *proc_buf;
    size_t proc_buf_size;
    size_t proc_frames_in;
    int16_t *ref_buf;
    size_t ref_buf_size;
    size_t ref_frames_in;
    int read_status;

    struct tuna_audio_device *dev;
};
enum {
    ORIENTATION_LANDSCAPE,
    ORIENTATION_PORTRAIT,
    ORIENTATION_SQUARE,
    ORIENTATION_UNDEFINED,
};

/**
 * NOTE: when multiple mutexes have to be acquired, always respect the following order:
 *        hw device > in stream > out stream
 */


static void select_output_device(struct tuna_audio_device *adev);
//static void select_input_device(struct tuna_audio_device *adev);
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int do_input_standby(struct tuna_stream_in *in);
static int do_output_standby(struct tuna_stream_out *out);

/* The enable flag when 0 makes the assumption that enums are disabled by
 * "Off" and integers/booleans by 0 */
static int set_route_by_array(struct mixer *mixer, struct route_setting *route,
                              int enable)
{
    struct mixer_ctl *ctl;
    unsigned int i, j;

    /* Go through the route array and set each value */
    i = 0;
    while (route[i].ctl_name) {
        ctl = mixer_get_ctl_by_name(mixer, route[i].ctl_name);
        if (!ctl)
            return -EINVAL;

        if (route[i].strval) {
            if (enable)
                mixer_ctl_set_enum_by_string(ctl, route[i].strval);
            else
                mixer_ctl_set_enum_by_string(ctl, "Off");
        } else {
            /* This ensures multiple (i.e. stereo) values are set jointly */
            for (j = 0; j < mixer_ctl_get_num_values(ctl); j++) {
                if (enable)
                    mixer_ctl_set_value(ctl, j, route[i].intval);
                else
                    mixer_ctl_set_value(ctl, j, 0);
            }
        }
        i++;
    }

    return 0;
}

static void force_all_standby(struct tuna_audio_device *adev)
{
    struct tuna_stream_in *in;
    struct tuna_stream_out *out;

    if (adev->active_output) {
        out = adev->active_output;
        pthread_mutex_lock(&out->lock);
        do_output_standby(out);
        pthread_mutex_unlock(&out->lock);
    }

    if (adev->active_input) {
        in = adev->active_input;
        pthread_mutex_lock(&in->lock);
        do_input_standby(in);
        pthread_mutex_unlock(&in->lock);
    }
}

int select_modeapp(int audiomode, int subdevice, int call_on)
{
   int ret = 0;
   int fd;
   int buf[2] = {audiomode, call_on ? 0 : 2}; // audioapp profile: 0 = voice call, 1 = voice call (WB), 2 = playback

   fd = open(PCG_IOCTL_DEVICE, O_RDWR);
   if (fd < 0)
      LOGE("ioctl failed: cannot open %s", PCG_IOCTL_DEVICE);

   ret = ioctl(fd, PCG_IOCTL_SETMODEAPP, buf);
   if (ret == -1)
      LOGE("%s: ioctl %d failed", PCG_IOCTL_DEVICE, PCG_IOCTL_SETMODEAPP);

   close(fd);

   return ret;
}

int select_volume(int volume, int voltype)
{
   int ret = 0;
   int fd;
   int type = voltype ? PCG_IOCTL_SETEXTPAVOL : PCG_IOCTL_SETVOL;

   fd = open(PCG_IOCTL_DEVICE, O_RDWR);
   if (fd < 0)
      LOGE("ioctl failed: cannot open %s", PCG_IOCTL_DEVICE);

   ret = ioctl(fd, type, &volume);
   if (ret == -1)
      LOGE("%s: ioctl %d failed", PCG_IOCTL_DEVICE, type);

   close(fd);

   return ret;
}

static void select_output_device(struct tuna_audio_device *adev)
{
    int earpiece_on;
    int headphone_on;
    int speaker_on;
    int call_on;
    int sco_on;
    int a2dp_on;

    call_on = adev->mode & AUDIO_MODE_IN_CALL;
    earpiece_on = adev->devices & AUDIO_DEVICE_OUT_EARPIECE;
    headphone_on = adev->devices & (AUDIO_DEVICE_OUT_WIRED_HEADSET |
                                    AUDIO_DEVICE_OUT_WIRED_HEADPHONE);
    speaker_on = adev->devices & AUDIO_DEVICE_OUT_SPEAKER;
    sco_on = adev->devices & AUDIO_DEVICE_OUT_ALL_SCO;
    a2dp_on = adev->devices & AUDIO_DEVICE_OUT_ALL_A2DP;

    bool extamp_volume = false;
    float volume_scale = 4.16; // max: 24
    int audiomode;
    set_route_by_array(adev->mixer, defaults, 1);

    if (speaker_on) {
        set_route_by_array(adev->mixer, speaker, speaker_on);
        audiomode = 4;
        adev->subdevice = 0;
    }
    if (earpiece_on) {
        set_route_by_array(adev->mixer, earpiece, earpiece_on);
        audiomode = 0;
        adev->subdevice = 2;
    }
    if (headphone_on) {
        set_route_by_array(adev->mixer, headphone, headphone_on);
        audiomode = 1;
        adev->subdevice = 4;
    }
    if (sco_on || a2dp_on) {
        set_route_by_array(adev->mixer, sco, 1);
        audiomode = 3;
        adev->subdevice = 6;
    }
    if (call_on)
        set_route_by_array(adev->mixer, mic, call_on);

    select_modeapp(audiomode, adev->subdevice, call_on);
    select_volume((100 / volume_scale), extamp_volume);

    LOGD("call=%c, earpiece=%c hp=%c speaker=%c sco=%c a2dp=%c out_device=%d", call_on ? 'y' : 'n', earpiece_on ? 'y' : 'n', headphone_on ? 'y' : 'n',
          speaker_on ? 'y' : 'n', sco_on ? 'y' : 'n', a2dp_on ? 'y' : 'n', adev->devices);
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct tuna_stream_out *out)
{
    struct tuna_audio_device *adev = out->dev;
    unsigned int flags = PCM_OUT | PCM_MMAP | PCM_NOIRQ;
    int i;
    bool success = true;
    unsigned int device;

    adev->active_output = out;

    /* default to low power: will be corrected in out_write if necessary before first write to
     * tinyalsa.
     */
//    out->write_threshold = PLAYBACK_LONG_PERIOD_COUNT * LONG_PERIOD_SIZE;

    if (adev->devices & AUDIO_DEVICE_OUT_ALL_SCO) {
        device = PCM_DEVICE_SCO;
        out->config = pcm_config_sco;
        out->write_threshold = SCO_PERIOD_COUNT * SCO_PERIOD_SIZE;
    } else {
        device = PCM_DEVICE;
        out->config = pcm_config_out;
        out->write_threshold = OUT_LONG_PERIOD_COUNT * OUT_PERIOD_SIZE;
        out->buffer_type = OUT_BUFFER_TYPE_UNKNOWN;
    }

    out->pcm = pcm_open(PCM_CARD, device, adev->subdevice, flags, &out->config);

    /* Close any PCMs that could not be opened properly and return an error */
    if (out->pcm && !pcm_is_ready(out->pcm)) {
        LOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        out->pcm = NULL;
        success = false;
    }

    if (success) {
        if (adev->echo_reference != NULL)
            out->echo_reference = adev->echo_reference;
        out->resampler->reset(out->resampler);
        return 0;
    }

    adev->active_output = NULL;
    return -ENOMEM;
}

static int check_input_parameters(uint32_t sample_rate, int format, int channel_count)
{
    if (format != AUDIO_FORMAT_PCM_16_BIT)
        return -EINVAL;

    if ((channel_count < 1) || (channel_count > 2))
        return -EINVAL;

    switch(sample_rate) {
    case 8000:
    case 11025:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static size_t get_input_buffer_size(uint32_t sample_rate, int format, int channel_count)
{
    size_t size;

    if (check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size = (pcm_config_in.period_size * sample_rate) / pcm_config_in.rate;
    size = ((size + 15) / 16) * 16;

    return size * channel_count * sizeof(short);
}

static void add_echo_reference(struct tuna_stream_out *out,
                               struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&out->lock);
    out->echo_reference = reference;
    pthread_mutex_unlock(&out->lock);
}

static void remove_echo_reference(struct tuna_stream_out *out,
                                  struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&out->lock);
    if (out->echo_reference == reference) {
        /* stop writing to echo reference */
        reference->write(reference, NULL);
        out->echo_reference = NULL;
    }
    pthread_mutex_unlock(&out->lock);
}

static void put_echo_reference(struct tuna_audio_device *adev,
                          struct echo_reference_itfe *reference)
{
    if (adev->echo_reference != NULL &&
            reference == adev->echo_reference) {
        if (adev->active_output != NULL)
            remove_echo_reference(adev->active_output, reference);
        release_echo_reference(reference);
        adev->echo_reference = NULL;
    }
}

static struct echo_reference_itfe *get_echo_reference(struct tuna_audio_device *adev,
                                               audio_format_t format,
                                               uint32_t channel_count,
                                               uint32_t sampling_rate)
{
    put_echo_reference(adev, adev->echo_reference);
    if (adev->active_output != NULL) {
        struct audio_stream *stream = &adev->active_output->stream.common;
        uint32_t wr_channel_count = popcount(stream->get_channels(stream));
        uint32_t wr_sampling_rate = stream->get_sample_rate(stream);

        int status = create_echo_reference(AUDIO_FORMAT_PCM_16_BIT,
                                           channel_count,
                                           sampling_rate,
                                           AUDIO_FORMAT_PCM_16_BIT,
                                           wr_channel_count,
                                           wr_sampling_rate,
                                           &adev->echo_reference);
        if (status == 0)
            add_echo_reference(adev->active_output, adev->echo_reference);
    }
    return adev->echo_reference;
}

static int get_playback_delay(struct tuna_stream_out *out,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{
    size_t kernel_frames;
    int status;
    int primary_pcm = 0;

    status = pcm_get_htimestamp(out->pcm, &kernel_frames, &buffer->time_stamp);
    if (status < 0) {
        buffer->time_stamp.tv_sec  = 0;
        buffer->time_stamp.tv_nsec = 0;
        buffer->delay_ns           = 0;
        LOGV("get_playback_delay(): pcm_get_htimestamp error,"
                "setting playbackTimestamp to 0");
        return status;
    }

    kernel_frames = pcm_get_buffer_size(out->pcm) - kernel_frames;

    /* adjust render time stamp with delay added by current driver buffer.
     * Add the duration of current frame as we want the render time of the last
     * sample being written. */
    buffer->delay_ns = (long)(((int64_t)(kernel_frames + frames)* 1000000000)/
                            OUT_SAMPLING_RATE);

    return 0;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    return OUT_SAMPLING_RATE;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct tuna_stream_out *out = (struct tuna_stream_out *)stream;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames. Note: we use the default rate here
    from pcm_config_mm.rate. */
    size_t size = (OUT_PERIOD_SIZE * OUT_SAMPLING_RATE) / pcm_config_out.rate;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_frame_size((struct audio_stream *)stream);

}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    return AUDIO_CHANNEL_OUT_STEREO;
}

static int out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, int format)
{
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct tuna_stream_out *out)
{
    struct tuna_audio_device *adev = out->dev;

    if (!out->standby) {
        pcm_close(out->pcm);
        out->pcm = NULL;
        adev->active_output = 0;

        /* stop writing to echo reference */
        if (out->echo_reference != NULL) {
            out->echo_reference->write(out->echo_reference, NULL);
            out->echo_reference = NULL;
        }

        out->standby = 1;
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct tuna_stream_out *out = (struct tuna_stream_out *)stream;
    int status;

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct tuna_stream_out *out = (struct tuna_stream_out *)stream;
    struct tuna_audio_device *adev = out->dev;
    struct tuna_stream_in *in;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    pthread_mutex_lock(&adev->lock);
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&out->lock);
        do_output_standby(out);
        if (((adev->devices & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0)) {
            adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
            adev->devices |= val;
            select_output_device(adev);
        }
        pthread_mutex_unlock(&out->lock);
    }
    pthread_mutex_unlock(&adev->lock);

    str_parms_destroy(parms);
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct tuna_stream_out *out = (struct tuna_stream_out *)stream;

    /*  Note: we use the default rate here from pcm_config_mm.rate */
//    return (SHORT_PERIOD_SIZE * PLAYBACK_SHORT_PERIOD_COUNT * 1000) / pcm_config_mm.rate;

    struct tuna_audio_device *adev = out->dev;
    size_t period_count;

    pthread_mutex_lock(&adev->lock);

    if (adev->screen_off && !adev->active_input && !(adev->devices & AUDIO_DEVICE_OUT_ALL_SCO))
        period_count = OUT_LONG_PERIOD_COUNT;
    else
        period_count = OUT_SHORT_PERIOD_COUNT;

    pthread_mutex_unlock(&adev->lock);

    return (pcm_config_out.period_size * period_count * 1000) / pcm_config_out.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct tuna_stream_out *out = (struct tuna_stream_out *)stream;
    struct tuna_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_frame_size(&out->stream.common);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = OUT_PERIOD_SIZE * 4 / frame_size;

    bool force_input_standby = false;
    struct tuna_stream_in *in;
    int kernel_frames;
    void *buf;
    /* If we're in out_write, we will find at least one pcm active */
    int primary_pcm = -1;
    int i;
    bool use_resampler = false;
    int period_size = 0;
    bool sco_on;
    int buffer_type;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = 0;
    }
    buffer_type = (adev->screen_off && !adev->active_input) ?
            OUT_BUFFER_TYPE_LONG : OUT_BUFFER_TYPE_SHORT;
    sco_on = (adev->devices & AUDIO_DEVICE_OUT_ALL_SCO);
    pthread_mutex_unlock(&adev->lock);

    if (!sco_on && (buffer_type != out->buffer_type)) {
        out->buffer_type = buffer_type;
    }

    if (out->pcm) {
        /* Make the first active PCM act as primary */
        if (period_size)
            pcm_set_avail_min(out->pcm, period_size);

        if (out->config.rate != OUT_SAMPLING_RATE)
            use_resampler = true;
    }
    /* only use resampler if required */

    if (use_resampler)
        out->resampler->resample_from_input(out->resampler,
                                            (int16_t *)buffer,
                                            &in_frames,
                                            (int16_t *)out->buffer,
                                            &out_frames);
    else
        out_frames = in_frames;

    if (out->echo_reference != NULL) {
        struct echo_reference_buffer b;
        b.raw = (void *)buffer;
        b.frame_count = in_frames;

        get_playback_delay(out, out_frames, &b);
        out->echo_reference->write(out->echo_reference, &b);
    }
    /* do not allow more than out->write_threshold frames in kernel pcm driver buffer */
    do {
        struct timespec time_stamp;

        if (pcm_get_htimestamp(out->pcm, (unsigned int *)&kernel_frames, &time_stamp) < 0)
            break;
        kernel_frames = pcm_get_buffer_size(out->pcm) - kernel_frames;

        if (kernel_frames > out->write_threshold) {
            unsigned long time = (unsigned long)
                    (((int64_t)(kernel_frames - out->write_threshold) * 1000000) /
                            out->config.rate);
            if (time < MIN_WRITE_SLEEP_US)
                time = MIN_WRITE_SLEEP_US;
            usleep(time);
        }
    } while (kernel_frames > out->write_threshold);
    if (out->pcm) {
        if (out->config.rate == OUT_SAMPLING_RATE) {
            /* PCM uses native sample rate */
            pcm_mmap_write(out->pcm, (void *)buffer, bytes);
        } else {
            /* PCM needs resampler */
            pcm_mmap_write(out->pcm, (void *)out->buffer, out_frames * frame_size);
        }
     }

exit:
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               out_get_sample_rate(&stream->common));
    }

    if (force_input_standby) {
        pthread_mutex_lock(&adev->lock);
        if (adev->active_input) {
            in = adev->active_input;
            pthread_mutex_lock(&in->lock);
            do_input_standby(in);
            pthread_mutex_unlock(&in->lock);
        }
        pthread_mutex_unlock(&adev->lock);
    }

    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

/** audio_stream_in implementation **/

/* must be called with hw device and input stream mutexes locked */
static int start_input_stream(struct tuna_stream_in *in)
{
    int ret = 0;
    unsigned int device;
    struct tuna_audio_device *adev = in->dev;

    adev->active_input = in;

    if (adev->devices & AUDIO_DEVICE_IN_ALL_SCO) {
        device = PCM_DEVICE_SCO;
        in->config = pcm_config_sco;
    } else {
        device = PCM_DEVICE;
        in->config = pcm_config_in;
    }

    if (adev->mode != AUDIO_MODE_IN_CALL) {
        adev->devices &= ~AUDIO_DEVICE_IN_ALL;
        adev->devices |= in->device;
//        select_input_device(adev);
    }

    if (in->need_echo_reference && in->echo_reference == NULL)
        in->echo_reference = get_echo_reference(adev,
                                        AUDIO_FORMAT_PCM_16_BIT,
                                        in->config.channels,
                                        in->requested_rate);

    /* this assumes routing is done previously */
    in->pcm = pcm_open(PCM_CARD, device, 0, PCM_IN, &in->config);
//    in->pcm = pcm_open(PCM_CARD, device, 0, PCM_IN, in->pcm_config);
    if (!pcm_is_ready(in->pcm)) {
        LOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        adev->active_input = NULL;
        return -ENOMEM;
    }

    /* if no supported sample rate is available, use the resampler */
    if (in->resampler) {
        in->resampler->reset(in->resampler);
        in->frames_in = 0;
    }
    return 0;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct tuna_stream_in *in = (struct tuna_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct tuna_stream_in *in = (struct tuna_stream_in *)stream;

    return get_input_buffer_size(in->requested_rate,
                                 AUDIO_FORMAT_PCM_16_BIT,
                                 in->config.channels);
}

static uint32_t in_get_channels(const struct audio_stream *stream)
{
    struct tuna_stream_in *in = (struct tuna_stream_in *)stream;

    if (in->config.channels == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

static int in_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, int format)
{
    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int do_input_standby(struct tuna_stream_in *in)
{
    struct tuna_audio_device *adev = in->dev;

    if (!in->standby) {
        pcm_close(in->pcm);
        in->pcm = NULL;

        adev->active_input = 0;
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            adev->devices &= ~AUDIO_DEVICE_IN_ALL;
//            select_input_device(adev);
        }

        if (in->echo_reference != NULL) {
            /* stop reading from echo reference */
            in->echo_reference->read(in->echo_reference, NULL);
            put_echo_reference(adev, in->echo_reference);
            in->echo_reference = NULL;
        }

        in->standby = 1;
    }
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct tuna_stream_in *in = (struct tuna_stream_in *)stream;
    int status;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct tuna_stream_in *in = (struct tuna_stream_in *)stream;
    struct tuna_audio_device *adev = in->dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;
    bool do_standby = false;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value, sizeof(value));

    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (ret >= 0) {
        val = atoi(value);
        /* no audio source uses val == 0 */
        if ((in->source != val) && (val != 0)) {
            in->source = val;
            do_standby = true;
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        if ((in->device != val) && (val != 0)) {
            in->device = val;
            select_output_device(adev);
            do_standby = true;
        }
    }

    if (do_standby)
        do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&adev->lock);

    str_parms_destroy(parms);
    return ret;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return 0;
}

static void get_capture_delay(struct tuna_stream_in *in,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{

    /* read frames available in kernel driver buffer */
    size_t kernel_frames;
    struct timespec tstamp;
    long buf_delay;
    long rsmp_delay;
    long kernel_delay;
    long delay_ns;

    if (pcm_get_htimestamp(in->pcm, &kernel_frames, &tstamp) < 0) {
        buffer->time_stamp.tv_sec  = 0;
        buffer->time_stamp.tv_nsec = 0;
        buffer->delay_ns           = 0;
        LOGW("read get_capture_delay(): pcm_htimestamp error");
        return;
    }

    /* read frames available in audio HAL input buffer
     * add number of frames being read as we want the capture time of first sample
     * in current buffer */
    buf_delay = (long)(((int64_t)(in->frames_in + in->proc_frames_in) * 1000000000)
                                    / in->config.rate);
    /* add delay introduced by resampler */
    rsmp_delay = 0;
    if (in->resampler) {
        rsmp_delay = in->resampler->delay_ns(in->resampler);
    }

    kernel_delay = (long)(((int64_t)kernel_frames * 1000000000) / in->config.rate);

    delay_ns = kernel_delay + buf_delay + rsmp_delay;

    buffer->time_stamp = tstamp;
    buffer->delay_ns   = delay_ns;
    LOGV("get_capture_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
         " kernel_delay:[%ld], buf_delay:[%ld], rsmp_delay:[%ld], kernel_frames:[%d], "
         "in->frames_in:[%d], in->proc_frames_in:[%d], frames:[%d]",
         buffer->time_stamp.tv_sec , buffer->time_stamp.tv_nsec, buffer->delay_ns,
         kernel_delay, buf_delay, rsmp_delay, kernel_frames,
         in->frames_in, in->proc_frames_in, frames);

}

static int32_t update_echo_reference(struct tuna_stream_in *in, size_t frames)
{
    struct echo_reference_buffer b;
    b.delay_ns = 0;

    LOGV("update_echo_reference, frames = [%d], in->ref_frames_in = [%d],  "
          "b.frame_count = [%d]",
         frames, in->ref_frames_in, frames - in->ref_frames_in);
    if (in->ref_frames_in < frames) {
        if (in->ref_buf_size < frames) {
            in->ref_buf_size = frames;
            in->ref_buf = (int16_t *)realloc(in->ref_buf,
                                             in->ref_buf_size *
                                                 in->config.channels * sizeof(int16_t));
        }

        b.frame_count = frames - in->ref_frames_in;
        b.raw = (void *)(in->ref_buf + in->ref_frames_in * in->config.channels);

        get_capture_delay(in, frames, &b);

        if (in->echo_reference->read(in->echo_reference, &b) == 0)
        {
            in->ref_frames_in += b.frame_count;
            LOGV("update_echo_reference: in->ref_frames_in:[%d], "
                    "in->ref_buf_size:[%d], frames:[%d], b.frame_count:[%d]",
                 in->ref_frames_in, in->ref_buf_size, frames, b.frame_count);
        }
    } else
        LOGW("update_echo_reference: NOT enough frames to read ref buffer");
    return b.delay_ns;
}

static int set_preprocessor_param(effect_handle_t handle,
                           effect_param_t *param)
{
    uint32_t size = sizeof(int);
    uint32_t psize = ((param->psize - 1) / sizeof(int) + 1) * sizeof(int) +
                        param->vsize;

    int status = (*handle)->command(handle,
                                   EFFECT_CMD_SET_PARAM,
                                   sizeof (effect_param_t) + psize,
                                   param,
                                   &size,
                                   &param->status);
    if (status == 0)
        status = param->status;

    return status;
}

static int set_preprocessor_echo_delay(effect_handle_t handle,
                                     int32_t delay_us)
{
    uint32_t buf[sizeof(effect_param_t) / sizeof(uint32_t) + 2];
    effect_param_t *param = (effect_param_t *)buf;

    param->psize = sizeof(uint32_t);
    param->vsize = sizeof(uint32_t);
    *(uint32_t *)param->data = AEC_PARAM_ECHO_DELAY;
    *((int32_t *)param->data + 1) = delay_us;

    return set_preprocessor_param(handle, param);
}

static void push_echo_reference(struct tuna_stream_in *in, size_t frames)
{
    /* read frames from echo reference buffer and update echo delay
     * in->ref_frames_in is updated with frames available in in->ref_buf */
    int32_t delay_us = update_echo_reference(in, frames)/1000;
    int i;
    audio_buffer_t buf;

    if (in->ref_frames_in < frames)
        frames = in->ref_frames_in;

    buf.frameCount = frames;
    buf.raw = in->ref_buf;

    for (i = 0; i < in->num_preprocessors; i++) {
        if ((*in->preprocessors[i])->process_reverse == NULL)
            continue;

        (*in->preprocessors[i])->process_reverse(in->preprocessors[i],
                                               &buf,
                                               NULL);
        set_preprocessor_echo_delay(in->preprocessors[i], delay_us);
    }

    in->ref_frames_in -= buf.frameCount;
    if (in->ref_frames_in) {
        memcpy(in->ref_buf,
               in->ref_buf + buf.frameCount * in->config.channels,
               in->ref_frames_in * in->config.channels * sizeof(int16_t));
    }
}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer)
{
    struct tuna_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    in = (struct tuna_stream_in *)((char *)buffer_provider -
                                   offsetof(struct tuna_stream_in, buf_provider));

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
        in->read_status = pcm_read(in->pcm,
                                   (void*)in->buffer,
                                   in->config.period_size *
                                       audio_stream_frame_size(&in->stream.common));
        if (in->read_status != 0) {
            LOGE("get_next_buffer() pcm_read error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->config.period_size;
    }

    buffer->frame_count = (buffer->frame_count > in->frames_in) ?
                                in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer + (in->config.period_size - in->frames_in) *
                                                in->config.channels;

    return in->read_status;

}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer)
{
    struct tuna_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return;

    in = (struct tuna_stream_in *)((char *)buffer_provider -
                                   offsetof(struct tuna_stream_in, buf_provider));

    in->frames_in -= buffer->frame_count;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames(struct tuna_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider(in->resampler,
                    (int16_t *)((char *)buffer +
                            frames_wr * audio_stream_frame_size(&in->stream.common)),
                    &frames_rd);
        } else {
            struct resampler_buffer buf = {
                    { raw : NULL, },
                    frame_count : frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                           frames_wr * audio_stream_frame_size(&in->stream.common),
                        buf.raw,
                        buf.frame_count * audio_stream_frame_size(&in->stream.common));
                frames_rd = buf.frame_count;
            }
            release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0)
            return in->read_status;

        frames_wr += frames_rd;
    }
    return frames_wr;
}

/* process_frames() reads frames from kernel driver (via read_frames()),
 * calls the active audio pre processings and output the number of frames requested
 * to the buffer specified */
static ssize_t process_frames(struct tuna_stream_in *in, void* buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;
    int i;

    while (frames_wr < frames) {
        /* first reload enough frames at the end of process input buffer */
        if (in->proc_frames_in < (size_t)frames) {
            ssize_t frames_rd;

            if (in->proc_buf_size < (size_t)frames) {
                in->proc_buf_size = (size_t)frames;
                in->proc_buf = (int16_t *)realloc(in->proc_buf,
                                         in->proc_buf_size *
                                             in->config.channels * sizeof(int16_t));
                LOGV("process_frames(): in->proc_buf %p size extended to %d frames",
                     in->proc_buf, in->proc_buf_size);
            }
            frames_rd = read_frames(in,
                                    in->proc_buf +
                                        in->proc_frames_in * in->config.channels,
                                    frames - in->proc_frames_in);
            if (frames_rd < 0) {
                frames_wr = frames_rd;
                break;
            }
            in->proc_frames_in += frames_rd;
        }

        if (in->echo_reference != NULL)
            push_echo_reference(in, in->proc_frames_in);

         /* in_buf.frameCount and out_buf.frameCount indicate respectively
          * the maximum number of frames to be consumed and produced by process() */
        in_buf.frameCount = in->proc_frames_in;
        in_buf.s16 = in->proc_buf;
        out_buf.frameCount = frames - frames_wr;
        out_buf.s16 = (int16_t *)buffer + frames_wr * in->config.channels;

        for (i = 0; i < in->num_preprocessors; i++)
            (*in->preprocessors[i])->process(in->preprocessors[i],
                                               &in_buf,
                                               &out_buf);

        /* process() has updated the number of frames consumed and produced in
         * in_buf.frameCount and out_buf.frameCount respectively
         * move remaining frames to the beginning of in->proc_buf */
        in->proc_frames_in -= in_buf.frameCount;
        if (in->proc_frames_in) {
            memcpy(in->proc_buf,
                   in->proc_buf + in_buf.frameCount * in->config.channels,
                   in->proc_frames_in * in->config.channels * sizeof(int16_t));
        }

        /* if not enough frames were passed to process(), read more and retry. */
        if (out_buf.frameCount == 0)
            continue;

        frames_wr += out_buf.frameCount;
    }
    return frames_wr;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    int ret = 0;
    struct tuna_stream_in *in = (struct tuna_stream_in *)stream;
    struct tuna_audio_device *adev = in->dev;
    size_t frames_rq = bytes / audio_stream_frame_size(&stream->common);

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the input stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ret = start_input_stream(in);
        if (ret == 0)
            in->standby = 0;
    }
    pthread_mutex_unlock(&adev->lock);

    if (ret < 0)
        goto exit;

    if (in->num_preprocessors != 0)
        ret = process_frames(in, buffer, frames_rq);
    else if (in->resampler != NULL)
        ret = read_frames(in, buffer, frames_rq);
    else
        ret = pcm_read(in->pcm, buffer, bytes);

    if (ret > 0)
        ret = 0;

    if (ret == 0 && adev->mic_mute)
        memset(buffer, 0, bytes);

exit:
    if (ret < 0)
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               in_get_sample_rate(&stream->common));

    pthread_mutex_unlock(&in->lock);
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    struct tuna_stream_in *in = (struct tuna_stream_in *)stream;
    int status;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors >= MAX_PREPROCESSORS) {
        status = -ENOSYS;
        goto exit;
    }

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;

    in->preprocessors[in->num_preprocessors++] = effect;

    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = true;
        do_input_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    struct tuna_stream_in *in = (struct tuna_stream_in *)stream;
    int i;
    int status = -EINVAL;
    bool found = false;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors <= 0) {
        status = -ENOSYS;
        goto exit;
    }

    for (i = 0; i < in->num_preprocessors; i++) {
        if (found) {
            in->preprocessors[i - 1] = in->preprocessors[i];
            continue;
        }
        if (in->preprocessors[i] == effect) {
            in->preprocessors[i] = NULL;
            status = 0;
            found = true;
        }
    }

    if (status != 0)
        goto exit;

    in->num_preprocessors--;

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;
    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = false;
        do_input_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}


static int adev_open_output_stream(struct audio_hw_device *dev,
                                   uint32_t devices, int *format,
                                   uint32_t *channels, uint32_t *sample_rate,
                                   struct audio_stream_out **stream_out)
{
    struct tuna_audio_device *ladev = (struct tuna_audio_device *)dev;
    struct tuna_stream_out *out;
    int ret;

    out = (struct tuna_stream_out *)calloc(1, sizeof(struct tuna_stream_out));
    if (!out)
        return -ENOMEM;

    ret = create_resampler(OUT_SAMPLING_RATE,
                           SCO_SAMPLING_RATE,
                           1,
                           RESAMPLER_QUALITY_DEFAULT,
                           NULL,
                           &out->resampler);

    if (ret != 0)
        goto err_open;

    out->buffer = malloc(OUT_PERIOD_SIZE * 4);

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;

    out->dev = ladev;
    out->standby = 1;

    /* FIXME: when we support multiple output devices, we will want to
     * do the following:
     * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
     * adev->devices |= out->device;
     * select_output_device(adev);
     * This is because out_set_parameters() with a route is not
     * guaranteed to be called after an output stream is opened. */

    *format = out_get_format(&out->stream.common);
    *channels = out_get_channels(&out->stream.common);
    *sample_rate = out_get_sample_rate(&out->stream.common);

    *stream_out = &out->stream;
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct tuna_stream_out *out = (struct tuna_stream_out *)stream;

    out_standby(&stream->common);
    if (out->buffer)
        free(out->buffer);
    if (out->resampler)
        release_resampler(out->resampler);
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct tuna_audio_device *adev = (struct tuna_audio_device *)dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret;

    parms = str_parms_create_str(kvpairs);
    ret = str_parms_get_str(parms, "orientation", value, sizeof(value));
    if (ret >= 0) {
        int orientation;

        if (strcmp(value, "landscape") == 0)
            orientation = ORIENTATION_LANDSCAPE;
        else if (strcmp(value, "portrait") == 0)
            orientation = ORIENTATION_PORTRAIT;
        else if (strcmp(value, "square") == 0)
            orientation = ORIENTATION_SQUARE;
        else
            orientation = ORIENTATION_UNDEFINED;

        pthread_mutex_lock(&adev->lock);
        if (orientation != adev->orientation) {
            adev->orientation = orientation;
            /*
             * Orientation changes can occur with the input device
             * closed so we must call select_devices() here to set
             * up the mixer. This is because select_devices() will
             * not be called when the input device is opened if no
             * other input parameter is changed.
             */
            select_output_device(adev);
        }
        pthread_mutex_unlock(&adev->lock);
    }

    ret = str_parms_get_str(parms, "screen_state", value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->screen_off = false;
        else
            adev->screen_off = true;
    }

    str_parms_destroy(parms);
    return ret;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct tuna_audio_device *adev = (struct tuna_audio_device *)dev;

    bool extamp_volume = false;
    int scaledvol;
    int amplitude = 128 * volume;
    if (amplitude == 128) amplitude = 127;
    float volume_scale = 4.16; // max: 24

    scaledvol = (amplitude / 1.27) / volume_scale;
    LOGV("voice volume: %.1f, extamp: %c, scale: %.1f, actual %d", volume, extamp_volume ? 'y' : 'n', volume_scale, scaledvol);
    select_volume(scaledvol, extamp_volume);

    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, int mode)
{
    struct tuna_audio_device *adev = (struct tuna_audio_device *)dev;

    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
    }
    pthread_mutex_unlock(&adev->lock);

    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct tuna_audio_device *adev = (struct tuna_audio_device *)dev;

    adev->mic_mute = state;

    LOGV("mic mute: %c", state ? 'y' : 'n');
    if (adev->mic_mute)
        set_route_by_array(adev->mixer, mic_off, 1);

    else
        set_route_by_array(adev->mixer, mic, 1);

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct tuna_audio_device *adev = (struct tuna_audio_device *)dev;

    *state = adev->mic_mute;

    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         uint32_t sample_rate, int format,
                                         int channel_count)
{
    size_t size;

    if (check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    return get_input_buffer_size(sample_rate, format, channel_count);
}

static int adev_open_input_stream(struct audio_hw_device *dev, uint32_t devices,
                                  int *format, uint32_t *channel_mask,
                                  uint32_t *sample_rate,
                                  audio_in_acoustics_t acoustics,
                                  struct audio_stream_in **stream_in)
{
    struct tuna_audio_device *ladev = (struct tuna_audio_device *)dev;
    struct tuna_stream_in *in;
    int ret;
    int channel_count = popcount(*channel_mask);

    if (check_input_parameters(*sample_rate, *format, channel_count) != 0)
        return -EINVAL;

    in = (struct tuna_stream_in *)calloc(1, sizeof(struct tuna_stream_in));
    if (!in)
        return -ENOMEM;

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    in->requested_rate = *sample_rate;

    memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
    in->config.channels = channel_count;

    in->buffer = malloc(in->config.period_size *
                        audio_stream_frame_size(&in->stream.common));
    if (!in->buffer) {
        ret = -ENOMEM;
        goto err;
    }

    if (in->requested_rate != in->config.rate) {
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;


        ret = create_resampler(in->config.rate,
                               in->requested_rate,
                               in->config.channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
        if (ret != 0) {
            ret = -EINVAL;
            goto err;
        }
/*
        if (in_get_sample_rate(&in->stream.common) != in->config.rate) {
            in->buf_provider.get_next_buffer = get_next_buffer;
            in->buf_provider.release_buffer = release_buffer;

            ret = create_resampler(in->config.rate,
                               in_get_sample_rate(&in->stream.common),
                               1,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
        }
*/
    }


    in->dev = ladev;
    in->standby = 1;
    in->device = devices;

    *stream_in = &in->stream;
    return 0;

err:
    if (in->resampler)
        release_resampler(in->resampler);

    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct tuna_stream_in *in = (struct tuna_stream_in *)stream;

    in_standby(&stream->common);

    if (in->resampler) {
        free(in->buffer);
        release_resampler(in->resampler);
    }
    if (in->proc_buf)
        free(in->proc_buf);
    if (in->ref_buf)
        free(in->ref_buf);

    free(stream);
    return;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct tuna_audio_device *adev = (struct tuna_audio_device *)device;

    mixer_close(adev->mixer);
    free(device);
    return 0;
}

static uint32_t adev_get_supported_devices(const struct audio_hw_device *dev)
{
    return (/* OUT */
            AUDIO_DEVICE_OUT_EARPIECE |
            AUDIO_DEVICE_OUT_SPEAKER |
            AUDIO_DEVICE_OUT_WIRED_HEADSET |
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
            AUDIO_DEVICE_OUT_AUX_DIGITAL |
            AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_ALL_SCO |
            AUDIO_DEVICE_OUT_DEFAULT |
            /* IN */
            AUDIO_DEVICE_IN_COMMUNICATION |
            AUDIO_DEVICE_IN_AMBIENT |
            AUDIO_DEVICE_IN_BUILTIN_MIC |
            AUDIO_DEVICE_IN_WIRED_HEADSET |
            AUDIO_DEVICE_IN_AUX_DIGITAL |
            AUDIO_DEVICE_IN_BACK_MIC |
            AUDIO_DEVICE_IN_ALL_SCO |
            AUDIO_DEVICE_IN_DEFAULT);
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct tuna_audio_device *adev;
    int ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct tuna_audio_device));
    if (!adev)
        return -ENOMEM;

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = 0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.get_supported_devices = adev_get_supported_devices;
    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;

    adev->mixer = mixer_open(0);
    if (!adev->mixer) {
        free(adev);
        LOGE("Unable to open the mixer, aborting.");
        return -EINVAL;
    }
    adev->mixer_ctls.speaker_enable = mixer_get_ctl_by_name(adev->mixer, "Speaker Playback Switch");
    adev->mixer_ctls.speaker_volume = mixer_get_ctl_by_name(adev->mixer, "Speaker Playback Volume");
    adev->mixer_ctls.earpiece_enable = mixer_get_ctl_by_name(adev->mixer, "Earpiece Playback Switch");
    adev->mixer_ctls.earpiece_volume = mixer_get_ctl_by_name(adev->mixer, "Earpiece Playback Volume");
    adev->mixer_ctls.headset_enable = mixer_get_ctl_by_name(adev->mixer, "Headset Playback Switch");
    adev->mixer_ctls.headset_volume = mixer_get_ctl_by_name(adev->mixer, "Headset Playback Volume");
    adev->mixer_ctls.mic_enable = mixer_get_ctl_by_name(adev->mixer, "Mic Capture Switch");
    adev->mixer_ctls.mic_volume = mixer_get_ctl_by_name(adev->mixer, "Mic Capture Volume");
    adev->mixer_ctls.sco_enable = mixer_get_ctl_by_name(adev->mixer, "BTHeadset Switch");
    adev->mixer_ctls.sco_volume = mixer_get_ctl_by_name(adev->mixer, "BTHeadset Volume");
    adev->mixer_ctls.pcm_route = mixer_get_ctl_by_name(adev->mixer, "PCM Playback Route");


    /* Set the default route before the PCM stream is opened */
    pthread_mutex_lock(&adev->lock);
    set_route_by_array(adev->mixer, defaults, 1);
    adev->mode = AUDIO_MODE_NORMAL;
    adev->devices = AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_IN_BUILTIN_MIC;

//    adev->voice_volume = 1.0f;
    adev->orientation = ORIENTATION_UNDEFINED;

    pthread_mutex_unlock(&adev->lock);

    *device = &adev->hw_device.common;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Tuna audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};
