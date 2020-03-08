/*
 * Copyright (c) 2019 arttttt <artem-bambalov@yandex.ru>
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

#define LOG_TAG "mipad_audio_wrapper"
/* #define LOG_NDEBUG 0  */

#include <errno.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <fcntl.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>

#include "audio_wrapper.h"

/* Set this variable to 1 to enable ALOGI */
int logwrapped = 0;

/* Input */
struct wrapper_in_stream {
    struct audio_stream_in *stream_in;
    struct nv_audio_stream_in *nv_stream_in;
    int in_use;
    pthread_mutex_t in_use_mutex;
    pthread_cond_t in_use_cond;
};

static struct wrapper_in_stream *in_streams = NULL;
static int n_in_streams = 0;
static pthread_mutex_t in_streams_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Output */
struct wrapper_out_stream {
    struct audio_stream_out *stream_out;
    struct nv_audio_stream_out *nv_stream_out;
    int in_use;
    pthread_mutex_t in_use_mutex;
    pthread_cond_t in_use_cond;
};

static struct wrapper_out_stream *out_streams = NULL;
static int n_out_streams = 0;
static pthread_mutex_t out_streams_mutex = PTHREAD_MUTEX_INITIALIZER;

/* HAL */
static struct nv_audio_hw_device *nv_hw_dev = NULL;
static void *dso_handle = NULL;
static int in_use = 0;
static pthread_mutex_t in_use_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t in_use_cond = PTHREAD_COND_INITIALIZER;

#define WAIT_FOR_FREE(in_use) do { pthread_mutex_lock(&(in_use ## _mutex)); \
                             while (in_use) { \
                                 pthread_cond_wait(&(in_use ## _cond), &(in_use ## _mutex)); \
                             } } while(0)

#define UNLOCK_FREE(in_use) do { pthread_cond_signal(&(in_use ## _cond)); \
                           pthread_mutex_unlock(&(in_use ## _mutex)); } while (0)

/* Generic wrappers for streams */
#define _WRAP_STREAM_LOCKED(name, function, direction, rettype, err, prototype, parameters, log) \
    static rettype wrapper_ ## direction ## _ ## name  prototype \
    { \
        rettype ret = err; \
        struct nv_audio_stream *nvstream; \
        struct nv_audio_stream_ ## direction *nvstream_ ## direction; \
        int i; \
    \
        if (logwrapped == 1) ALOGI log; \
        pthread_mutex_lock(& direction ## _streams_mutex); \
        for (i = 0; i < n_ ## direction ## _streams; i++) { \
            if (direction ## _streams[i].stream_ ## direction == (struct audio_stream_ ## direction*)stream) { \
                WAIT_FOR_FREE(direction ## _streams[i].in_use); \
                nvstream = (struct nv_audio_stream *)direction ## _streams[i].nv_stream_ ## direction; \
                nvstream_ ## direction = direction ## _streams[i].nv_stream_ ## direction; \
                ret = nvstream_ ## direction ->function parameters; \
                UNLOCK_FREE(direction ## _streams[i].in_use); \
                break; \
            } \
        } \
        pthread_mutex_unlock(& direction ## _streams_mutex); \
    \
        return ret; \
    }

#define WRAP_STREAM_LOCKED(name, direction, rettype, err, prototype, parameters, log) \
        _WRAP_STREAM_LOCKED(name, name, direction, rettype, err, prototype, parameters, log)


#define WRAP_STREAM_LOCKED_COMMON(name, direction, rettype, err, prototype, parameters, log) \
_WRAP_STREAM_LOCKED(name, common.name, direction, rettype, err, prototype, parameters, log)

/* Generic wrappers for HAL */
#define _WRAP_HAL_LOCKED(name, function, prototype, parameters, log) \
    static int wrapper_ ## name  prototype \
    { \
        int ret; \
    \
        if (logwrapped == 1) ALOGI log; \
        pthread_mutex_lock(&out_streams_mutex); \
        pthread_mutex_lock(&in_streams_mutex); \
    \
        WAIT_FOR_FREE(in_use); \
        ret = nv_hw_dev->function parameters; \
        UNLOCK_FREE(in_use); \
    \
        pthread_mutex_unlock(&in_streams_mutex); \
        pthread_mutex_unlock(&out_streams_mutex); \
    \
        return ret; \
    }

#define WRAP_HAL_LOCKED(name, prototype, parameters, log) \
        _WRAP_HAL_LOCKED(name, name, prototype, parameters, log)

#define _WRAP_HAL(name, function, rettype, prototype, parameters, log) \
    static rettype wrapper_ ## name  prototype \
    { \
        if (logwrapped == 1) ALOGI log; \
    \
        return nv_hw_dev->function parameters; \
    }

#define WRAP_HAL(name, rettype, prototype, parameters, log) \
        _WRAP_HAL(name, name, rettype, prototype, parameters, log)

/* Unused parameters */
#define unused_audio_hw_device  __attribute__((unused)) struct audio_hw_device

/* Input stream */

WRAP_STREAM_LOCKED(read, in, ssize_t, -ENODEV, (struct audio_stream_in *stream, void* buffer, size_t bytes),
            (nvstream_in, buffer, bytes), ("in_read"))

WRAP_STREAM_LOCKED(set_gain, in, int, -ENODEV, (struct audio_stream_in *stream, float gain),
            (nvstream_in, gain), ("in_set_gain: %f", gain))

WRAP_STREAM_LOCKED_COMMON(standby, in, int, -ENODEV, (struct audio_stream *stream),
            (nvstream), ("in_standby"))

//WRAP_STREAM_LOCKED_COMMON(set_parameters, in, int, -ENODEV, (struct audio_stream *stream, const char *kv_pairs),
//(nvstream, kv_pairs), ("in_set_parameters: %s", kv_pairs))

WRAP_STREAM_LOCKED(get_input_frames_lost, in, uint32_t, -ENODEV, (struct audio_stream_in *stream), (nvstream), ("in_get_input_frames_lost"))

WRAP_STREAM_LOCKED_COMMON(get_sample_rate, in, uint32_t, 0, (const struct audio_stream *stream),
            (nvstream), ("in_get_sample_rate"))

WRAP_STREAM_LOCKED_COMMON(set_sample_rate, in, int, -ENODEV, (struct audio_stream *stream, uint32_t rate),
            (nvstream, rate), ("in_set_sample_rate: %u", rate))

WRAP_STREAM_LOCKED_COMMON(get_buffer_size, in, size_t, 0, (const struct audio_stream *stream),
            (nvstream), ("in_get_buffer_size"))

WRAP_STREAM_LOCKED_COMMON(get_channels, in, audio_channel_mask_t, 0, (const struct audio_stream *stream),
            (nvstream), ("in_get_channels"))

WRAP_STREAM_LOCKED_COMMON(get_format, in, audio_format_t, 0, (const struct audio_stream *stream),
            (nvstream), ("in_get_format"))

WRAP_STREAM_LOCKED_COMMON(set_format, in, int, -ENODEV, (struct audio_stream *stream, audio_format_t format),
            (nvstream, format), ("in_set_format: %u", format))

WRAP_STREAM_LOCKED_COMMON(dump, in, int, -ENODEV, (const struct audio_stream *stream, int fd),
            (nvstream, fd), ("in_dump: %d", fd))

WRAP_STREAM_LOCKED_COMMON(get_device, in, audio_devices_t, 0, (const struct audio_stream *stream),
            (nvstream), ("in_get_device"))

WRAP_STREAM_LOCKED_COMMON(set_device, in, int, -ENODEV, (struct audio_stream *stream, audio_devices_t device),
            (nvstream, device), ("in_set_device: %d", device))

WRAP_STREAM_LOCKED_COMMON(get_parameters, in, char*, NULL, (const struct audio_stream *stream, const char *keys),
            (nvstream, keys), ("in_get_parameters: %s", keys))

WRAP_STREAM_LOCKED_COMMON(add_audio_effect, in, int, -ENODEV, (const struct audio_stream *stream, effect_handle_t effect),
            (nvstream, effect), ("in_add_audio_effect"))

WRAP_STREAM_LOCKED_COMMON(remove_audio_effect, in, int, -ENODEV, (const struct audio_stream *stream, effect_handle_t effect),
            (nvstream, effect), ("in_remove_audio_effect"))

static void wrapper_close_input_stream(unused_audio_hw_device *dev,
                                       struct audio_stream_in *stream_in)
{
    struct nv_audio_stream_in *nv_stream_in = NULL;
    int i;

    pthread_mutex_lock(&in_streams_mutex);
    for (i = 0; i < n_in_streams; i++) {
        if (in_streams[i].stream_in == stream_in) {
            WAIT_FOR_FREE(in_streams[i].in_use);
            UNLOCK_FREE(in_streams[i].in_use);
            nv_stream_in = in_streams[i].nv_stream_in;
            free(in_streams[i].stream_in);
            pthread_mutex_destroy(&(in_streams[i].in_use_mutex));
            pthread_cond_destroy(&(in_streams[i].in_use_cond));
            n_in_streams--;
            memmove(in_streams + i,
                    in_streams + i + 1,
                    sizeof(struct wrapper_in_stream) * (n_in_streams - i));
            in_streams = realloc(in_streams,
                                  sizeof(struct wrapper_in_stream) * n_in_streams);
            if (logwrapped == 1) ALOGI("Closed wrapped input stream");
            break;
        }
    }
    if (nv_stream_in) {
        WAIT_FOR_FREE(in_use);
        nv_hw_dev->close_input_stream(nv_hw_dev, nv_stream_in);
        UNLOCK_FREE(in_use);
    }

    pthread_mutex_unlock(&in_streams_mutex);
}

static int wrapper_get_capture_position(__attribute__((unused))const struct audio_stream_in *stream,
                                   __attribute__((unused))int64_t *frames, __attribute__((unused))int64_t *time)
{
    return 0;
}

#define AUDIO_DEVICE_IN_BUILTIN_MIC_NAME 		";routing_by_name=builtin-mic"
#define AUDIO_DEVICE_IN_WIRED_HEADSET_NAME 		";routing_by_name=headset-mic"
#define AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET_NAME 	";routing_by_name=bt-sco-mic"

static int wrapper_in_set_parameters(struct audio_stream *stream, const char *kv_pairs)
{
    int i;
    int ret = -ENODEV;
    struct nv_audio_stream *nvstream;
    struct nv_audio_stream_in *nvstream_in;
    struct str_parms *parms;
    char value[32];

    char *kv_pairs_new = kv_pairs;

    if (logwrapped == 1) ALOGI("in_set_parameters: %s", kv_pairs);
    pthread_mutex_lock(&in_streams_mutex);
    for (i = 0; i < n_in_streams; i++) {
        if (in_streams[i].stream_in == (struct audio_stream_in*)stream) {
            WAIT_FOR_FREE(in_streams[i].in_use);
            parms = str_parms_create_str(kv_pairs);
            ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
            if (ret >= 0) {
                uint32_t new_devices = atoi(value);
                ALOGI("new_devices_orig: %d", new_devices);
                new_devices &= ~AUDIO_DEVICE_BIT_IN;

                ALOGI("new_devices: %d", new_devices);

                ALOGI("new_devices == AUDIO_DEVICE_IN_BUILTIN_MIC: %d", new_devices & AUDIO_DEVICE_IN_BUILTIN_MIC);
                ALOGI("new_devices == AUDIO_DEVICE_IN_WIRED_HEADSET: %d", new_devices & AUDIO_DEVICE_IN_WIRED_HEADSET);
                ALOGI("new_devices == AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET: %d", new_devices & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET);

                if (new_devices & AUDIO_DEVICE_IN_BUILTIN_MIC) {
                    strcat(kv_pairs_new, AUDIO_DEVICE_IN_BUILTIN_MIC_NAME);
                } else if (new_devices & AUDIO_DEVICE_IN_WIRED_HEADSET) {
                    strcat(kv_pairs_new, AUDIO_DEVICE_IN_WIRED_HEADSET_NAME);
                } else if (new_devices & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
                    strcat(kv_pairs_new, AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET_NAME);
                }

                ALOGI("in_set_parameters: %s", kv_pairs_new);

                nvstream = (struct nv_audio_stream *)in_streams[i].nv_stream_in;
                nvstream_in = in_streams[i].nv_stream_in;
                ret = nvstream_in->common.set_parameters(nvstream, kv_pairs);
            }
            UNLOCK_FREE(in_streams[i].in_use);
            break;
        }
    }
    pthread_mutex_unlock(&in_streams_mutex);

    return ret;
}

static int wrapper_open_input_stream(unused_audio_hw_device *dev,
                                     audio_io_handle_t handle,
                                     audio_devices_t devices,
                                     struct audio_config *config,
                                     struct audio_stream_in **stream_in,
                                     __attribute__((unused)) audio_input_flags_t flags,
                                     __attribute__((unused)) const char *address,
                                     __attribute__((unused)) audio_source_t source)
{
    struct nv_audio_stream_in *nv_stream_in;
    int ret;

    pthread_mutex_lock(&in_streams_mutex);

    WAIT_FOR_FREE(in_use);
    ret = nv_hw_dev->open_input_stream(nv_hw_dev, handle, devices,
                                         config, &nv_stream_in);
    UNLOCK_FREE(in_use);

    if (ret == 0) {
        struct wrapper_in_stream *new_in_streams;

        new_in_streams = realloc(in_streams,
                              sizeof(struct wrapper_in_stream) * (n_in_streams + 1));
        if (!new_in_streams) {
            ALOGE("Can't allocate memory for wrapped stream, not touching original!");
            pthread_mutex_unlock(&in_streams_mutex);
            return -ENOMEM;
        }
        in_streams = new_in_streams;
        memset(&in_streams[n_in_streams], 0, sizeof(struct wrapper_in_stream));

        in_streams[n_in_streams].nv_stream_in = nv_stream_in;
        in_streams[n_in_streams].stream_in = malloc(sizeof(struct audio_stream_in));
        if (!in_streams[n_in_streams].stream_in) {
            ALOGE("Can't allocate memory for stream_in!");
            pthread_mutex_unlock(&in_streams_mutex);
            return -ENOMEM;
        }
        memset(in_streams[n_in_streams].stream_in, 0, sizeof(struct audio_stream_in));
        *stream_in = in_streams[n_in_streams].stream_in;

        (*stream_in)->common.get_sample_rate = wrapper_in_get_sample_rate;
        (*stream_in)->common.set_sample_rate = wrapper_in_set_sample_rate;
        (*stream_in)->common.get_buffer_size = wrapper_in_get_buffer_size;
        (*stream_in)->common.get_channels = wrapper_in_get_channels;
        (*stream_in)->common.get_format = wrapper_in_get_format;
        (*stream_in)->common.set_format = wrapper_in_set_format;
        (*stream_in)->common.standby = wrapper_in_standby;
        (*stream_in)->common.dump = wrapper_in_dump;
        (*stream_in)->common.get_device = wrapper_in_get_device;
        (*stream_in)->common.set_device = wrapper_in_set_device;
        (*stream_in)->common.set_parameters = wrapper_in_set_parameters;
        (*stream_in)->common.get_parameters = wrapper_in_get_parameters;
        (*stream_in)->common.add_audio_effect = wrapper_in_add_audio_effect;
        (*stream_in)->common.remove_audio_effect = wrapper_in_remove_audio_effect;

        (*stream_in)->set_gain = wrapper_in_set_gain;
        (*stream_in)->read = wrapper_in_read;
        (*stream_in)->get_input_frames_lost = wrapper_in_get_input_frames_lost;
        (*stream_in)->get_capture_position = wrapper_get_capture_position;

        in_streams[n_in_streams].in_use = 0;
        pthread_mutex_init(&(in_streams[n_in_streams].in_use_mutex), NULL);
        pthread_cond_init(&(in_streams[n_in_streams].in_use_cond), NULL);

        if (logwrapped == 1){
            ALOGI("Wrapped an input stream: rate %d, channel_mask: %x, format: %d, addr: %p/%p",
                  config->sample_rate, config->channel_mask, config->format, *stream_in, nv_stream_in);
        }
        n_in_streams++;
    }
    pthread_mutex_unlock(&in_streams_mutex);

    return ret;
}

WRAP_STREAM_LOCKED(write, out, int, -ENODEV, (struct audio_stream_out *stream, const void* buffer, size_t bytes),
            (nvstream_out, buffer, bytes), ("out_write"))

WRAP_STREAM_LOCKED(set_volume, out, int, -ENODEV, (struct audio_stream_out *stream, float left, float right),
            (nvstream_out, left, right), ("set_out_volume: %f/%f", left, right))

WRAP_STREAM_LOCKED_COMMON(standby, out, int, -ENODEV, (struct audio_stream *stream),
            (nvstream), ("out_standby"))

WRAP_STREAM_LOCKED_COMMON(set_parameters, out, int, -ENODEV, (struct audio_stream *stream, const char *kv_pairs),
            (nvstream, kv_pairs), ("out_set_parameters: %s", kv_pairs))

WRAP_STREAM_LOCKED_COMMON(get_sample_rate, out, uint32_t, 0, (const struct audio_stream *stream),
            (nvstream), ("out_get_sample_rate"))

WRAP_STREAM_LOCKED_COMMON(set_sample_rate, out, int, -ENODEV, (struct audio_stream *stream, uint32_t rate),
            (nvstream, rate), ("out_set_sample_rate: %u", rate))

WRAP_STREAM_LOCKED_COMMON(get_buffer_size, out, size_t, 0, (const struct audio_stream *stream),
            (nvstream), ("out_get_buffer_size"))

WRAP_STREAM_LOCKED_COMMON(get_channels, out, audio_channel_mask_t, 0, (const struct audio_stream *stream),
            (nvstream), ("out_get_channels"))

WRAP_STREAM_LOCKED_COMMON(get_format, out, audio_format_t, 0, (const struct audio_stream *stream),
            (nvstream), ("out_get_format"))

WRAP_STREAM_LOCKED_COMMON(set_format, out, int, -ENODEV, (struct audio_stream *stream, audio_format_t format),
            (nvstream, format), ("out_set_format: %u", format))

WRAP_STREAM_LOCKED_COMMON(dump, out, int, -ENODEV, (const struct audio_stream *stream, int fd),
            (nvstream, fd), ("out_dump: %d", fd))

WRAP_STREAM_LOCKED_COMMON(get_device, out, audio_devices_t, 0, (const struct audio_stream *stream),
            (nvstream), ("out_get_device"))

WRAP_STREAM_LOCKED_COMMON(set_device, out, int, -ENODEV, (struct audio_stream *stream, audio_devices_t device),
            (nvstream, device), ("out_set_device: %d", device))

WRAP_STREAM_LOCKED_COMMON(get_parameters, out, char*, NULL, (const struct audio_stream *stream, const char *keys),
            (nvstream, keys), ("out_get_parameters: %s", keys))

WRAP_STREAM_LOCKED_COMMON(add_audio_effect, out, int, -ENODEV, (const struct audio_stream *stream, effect_handle_t effect),
            (nvstream, effect), ("out_add_audio_effect"))

WRAP_STREAM_LOCKED_COMMON(remove_audio_effect, out, int, -ENODEV, (const struct audio_stream *stream, effect_handle_t effect),
            (nvstream, effect), ("out_remove_audio_effect"))

WRAP_STREAM_LOCKED(get_latency, out, uint32_t, 0, (const struct audio_stream_out *stream),
            (nvstream_out), ("out_get_latency"))

WRAP_STREAM_LOCKED(get_render_position, out, int, -ENODEV, (const struct audio_stream_out *stream, uint32_t *dsp_frames),
            (nvstream_out, dsp_frames), ("out_get_render_position"))

WRAP_STREAM_LOCKED(get_next_write_timestamp, out, int, -ENODEV, (const struct audio_stream_out *stream, int64_t *timestamp),
            (nvstream_out, timestamp), NULL)

WRAP_STREAM_LOCKED(set_callback, out, int, -ENODEV, (struct audio_stream_out *stream, stream_callback_t callback, void *cookie),	
				   (nvstream_out, callback, cookie), NULL)

WRAP_STREAM_LOCKED(pause, out, int, -ENODEV, (struct audio_stream_out* stream), (nvstream_out), NULL)

WRAP_STREAM_LOCKED(resume, out, int, -ENODEV, (struct audio_stream_out* stream), (nvstream_out), NULL)

WRAP_STREAM_LOCKED(drain, out, int, -ENODEV, (struct audio_stream_out* stream, audio_drain_type_t type ), (nvstream_out, type), NULL)

WRAP_STREAM_LOCKED(flush, out, int, -ENODEV, (struct audio_stream_out* stream), (nvstream_out), NULL)

WRAP_STREAM_LOCKED(get_presentation_position, out, int, -ENODEV, (const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp),
            (nvstream_out, frames, timestamp), ("out_get_presentation_position"))

static void wrapper_close_output_stream(unused_audio_hw_device *dev,
                            struct audio_stream_out* stream_out)
{
    struct nv_audio_stream_out *nv_stream_out = NULL;
    int i;

    pthread_mutex_lock(&out_streams_mutex);
    for (i = 0; i < n_out_streams; i++) {
        if (out_streams[i].stream_out == stream_out) {
            WAIT_FOR_FREE(out_streams[i].in_use);
            UNLOCK_FREE(out_streams[i].in_use);
            nv_stream_out = out_streams[i].nv_stream_out;
            free(out_streams[i].stream_out);
            pthread_mutex_destroy(&(out_streams[i].in_use_mutex));
            pthread_cond_destroy(&(out_streams[i].in_use_cond));
            n_out_streams--;
            memmove(out_streams + i,
                    out_streams + i + 1,
                    sizeof(struct wrapper_out_stream) * (n_out_streams - i));
            out_streams = realloc(out_streams,
                                  sizeof(struct wrapper_out_stream) * n_out_streams);
            if (logwrapped == 1) ALOGI("Closed wrapped output stream");
            break;
        }
    }

    if (nv_stream_out) {
        WAIT_FOR_FREE(in_use);
        nv_hw_dev->close_output_stream(nv_hw_dev, nv_stream_out);
        UNLOCK_FREE(in_use);
    }

    pthread_mutex_unlock(&out_streams_mutex);
}

static int wrapper_open_output_stream(unused_audio_hw_device *dev,
                                      audio_io_handle_t handle,
                                      audio_devices_t devices,
                                      audio_output_flags_t flags,
                                      struct audio_config *config,
                                      struct audio_stream_out **stream_out,
                                      __attribute__((unused)) const char *address)
{
    struct nv_audio_stream_out *nv_stream_out;
    int ret;

    pthread_mutex_lock(&out_streams_mutex);

    WAIT_FOR_FREE(in_use);
    ret = nv_hw_dev->open_output_stream(nv_hw_dev, handle, devices,
                                          flags, config, &nv_stream_out);
    UNLOCK_FREE(in_use);

    if (ret == 0) {
        struct wrapper_out_stream *new_out_streams;

        new_out_streams = realloc(out_streams,
                              sizeof(struct wrapper_out_stream) * (n_out_streams + 1));
        if (!new_out_streams) {
            ALOGE("Can't allocate memory for wrapped stream, not touching original!");
            pthread_mutex_unlock(&out_streams_mutex);
            return -ENOMEM;
        }
        out_streams = new_out_streams;
        memset(&out_streams[n_out_streams], 0, sizeof(struct wrapper_out_stream));

        out_streams[n_out_streams].nv_stream_out = nv_stream_out;
        out_streams[n_out_streams].stream_out = malloc(sizeof(struct audio_stream_out));
        if (!out_streams[n_out_streams].stream_out) {
            ALOGE("Can't allocate memory for stream_out!");
            pthread_mutex_unlock(&out_streams_mutex);
            return -ENOMEM;
        }
        memset(out_streams[n_out_streams].stream_out, 0, sizeof(struct audio_stream_out));
        *stream_out = out_streams[n_out_streams].stream_out;

        (*stream_out)->common.get_sample_rate = wrapper_out_get_sample_rate;
        (*stream_out)->common.set_sample_rate = wrapper_out_set_sample_rate;
        (*stream_out)->common.get_buffer_size = wrapper_out_get_buffer_size;
        (*stream_out)->common.get_channels = wrapper_out_get_channels;
        (*stream_out)->common.get_format = wrapper_out_get_format;
        (*stream_out)->common.set_format = wrapper_out_set_format;
        (*stream_out)->common.standby = wrapper_out_standby;
        (*stream_out)->common.dump = wrapper_out_dump;
        (*stream_out)->common.get_device = wrapper_out_get_device;
        (*stream_out)->common.set_device = wrapper_out_set_device;
        (*stream_out)->common.set_parameters = wrapper_out_set_parameters;
        (*stream_out)->common.get_parameters = wrapper_out_get_parameters;
        (*stream_out)->common.add_audio_effect = wrapper_out_add_audio_effect;
        (*stream_out)->common.remove_audio_effect = wrapper_out_remove_audio_effect;

        (*stream_out)->get_latency = wrapper_out_get_latency;
        (*stream_out)->set_volume = wrapper_out_set_volume;
        (*stream_out)->write = wrapper_out_write;
        (*stream_out)->get_render_position = wrapper_out_get_render_position;
        (*stream_out)->get_next_write_timestamp = wrapper_out_get_next_write_timestamp;
        (*stream_out)->set_callback = wrapper_out_set_callback;
        (*stream_out)->pause = wrapper_out_pause;
        (*stream_out)->resume = wrapper_out_resume;
        (*stream_out)->drain = wrapper_out_drain;
        (*stream_out)->flush = wrapper_out_flush;
        (*stream_out)->get_presentation_position = wrapper_out_get_presentation_position;

        out_streams[n_out_streams].in_use = 0;
        pthread_mutex_init(&(out_streams[n_out_streams].in_use_mutex), NULL);
        pthread_cond_init(&(out_streams[n_out_streams].in_use_cond), NULL);

        if (logwrapped == 1) {
            ALOGI("Wrapped an output stream: rate %d, channel_mask: %x, format: %d, addr: %p/%p",
                  config->sample_rate, config->channel_mask, config->format, *stream_out, nv_stream_out);
        }
        n_out_streams++;
    }
    pthread_mutex_unlock(&out_streams_mutex);

    return ret;
}

static int wrapper_set_mode(unused_audio_hw_device *dev, audio_mode_t mode)
{
    int ret;

    if (logwrapped == 1) ALOGI("set_mode: %d", mode);

    pthread_mutex_lock(&out_streams_mutex);
    pthread_mutex_lock(&in_streams_mutex);
    WAIT_FOR_FREE(in_use);
    ret = nv_hw_dev->set_mode(nv_hw_dev, mode);
    UNLOCK_FREE(in_use);
    pthread_mutex_unlock(&in_streams_mutex);
    pthread_mutex_unlock(&out_streams_mutex);

    return ret;
}

WRAP_HAL_LOCKED(set_master_volume, (unused_audio_hw_device *dev, float volume),
                (nv_hw_dev, volume), ("set_master_volume: %f", volume))

WRAP_HAL_LOCKED(set_mic_mute, (unused_audio_hw_device *dev, bool state),
                (nv_hw_dev, state), ("set_mic_mute: %d", state))

WRAP_HAL_LOCKED(set_voice_volume, (unused_audio_hw_device *dev, float volume),
                (nv_hw_dev, volume), ("set_voice_volume: %f", volume))

WRAP_HAL_LOCKED(set_parameters, (unused_audio_hw_device *dev, const char *kv_pairs),
                (nv_hw_dev, kv_pairs), ("set_parameters: %s", kv_pairs))

WRAP_HAL(get_supported_devices, uint32_t, (const unused_audio_hw_device *dev),
         (nv_hw_dev), ("get_supported_devices"))

WRAP_HAL(init_check, int, (const unused_audio_hw_device *dev),
         (nv_hw_dev), ("init_check"))

WRAP_HAL(get_mic_mute, int, (const unused_audio_hw_device *dev, bool *state),
         (nv_hw_dev, state), ("get_mic_mute"))

WRAP_HAL(get_parameters, char*, (const unused_audio_hw_device *dev, const char *keys),
         (nv_hw_dev, keys), ("get_parameters: %s", keys))

WRAP_HAL(get_input_buffer_size, size_t, (const unused_audio_hw_device *dev, const struct audio_config *config),
         (nv_hw_dev, config), ("get_input_buffer_size"))

WRAP_HAL(dump, int, (const unused_audio_hw_device *dev, int fd),
         (nv_hw_dev, fd), ("dump"))

static int wrapper_close(hw_device_t *device)
{
    int ret;

    pthread_mutex_lock(&out_streams_mutex);
    pthread_mutex_lock(&in_streams_mutex);

    WAIT_FOR_FREE(in_use);

    ret = nv_hw_dev->common.close(device);

    dlclose(dso_handle);
    dso_handle = NULL;
    free(nv_hw_dev);
    nv_hw_dev = NULL;

    if (out_streams) {
        free(out_streams);
        out_streams = NULL;
        n_out_streams = 0;
    }

    if (in_streams) {
        free(in_streams);
        in_streams = NULL;
        n_in_streams = 0;
    }

    UNLOCK_FREE(in_use);
    pthread_mutex_unlock(&in_streams_mutex);
    pthread_mutex_unlock(&out_streams_mutex);

    return ret;
}

static int wrapper_open(__attribute__((unused)) const hw_module_t* module,
                             __attribute__((unused)) const char* name,
                             hw_device_t** device)
{
    struct hw_module_t *hmi;
    struct audio_hw_device *adev;
    int ret;

    ALOGI("Initializing wrapper for MIPAD audio-HAL");
    if (nv_hw_dev) {
        ALOGE("Audio HAL already opened!");
        return -ENODEV;
    }

    dso_handle = dlopen("/system/vendor/lib/hw/audio.primary.vendor.tegra.so", RTLD_NOW);
    if (dso_handle == NULL) {
        char const *err_str = dlerror();
        ALOGE("wrapper_open: %s", err_str ? err_str : "unknown");
        return -EINVAL;
    }

    const char *sym = HAL_MODULE_INFO_SYM_AS_STR;
    hmi = (struct hw_module_t *)dlsym(dso_handle, sym);
    if (hmi == NULL) {
        ALOGE("wrapper_open: couldn't find symbol %s", sym);
        dlclose(dso_handle);
        dso_handle = NULL;
        return -EINVAL;
    }

    hmi->dso = dso_handle;

    ret = audio_hw_device_open(hmi, (struct audio_hw_device**)&nv_hw_dev);
    ALOGE_IF(ret, "%s couldn't open audio module in %s. (%s)", __func__,
                 AUDIO_HARDWARE_MODULE_ID, strerror(-ret));
    if (ret) {
        dlclose(dso_handle);
        dso_handle = NULL;
        return ret;
    }

    *device = malloc(sizeof(struct audio_hw_device));
    if (!*device) {
        ALOGE("Can't allocate memory for device, aborting...");
        dlclose(dso_handle);
        dso_handle = NULL;

        return -ENOMEM;
    }

    memset(*device, 0, sizeof(struct audio_hw_device));

    adev = (struct audio_hw_device*)*device;

    /* HAL */
    adev->common.tag = HARDWARE_DEVICE_TAG;
    adev->common.version = AUDIO_DEVICE_API_VERSION_MIN;
    adev->common.module = (struct hw_module_t *) module;
    adev->common.close = wrapper_close;

    adev->get_supported_devices = wrapper_get_supported_devices;
    adev->init_check = wrapper_init_check;
    adev->set_voice_volume = wrapper_set_voice_volume;
    adev->set_master_volume = wrapper_set_master_volume;
    adev->set_mic_mute = wrapper_set_mic_mute;
    adev->get_mic_mute = wrapper_get_mic_mute;
    adev->get_parameters = wrapper_get_parameters;
    adev->get_input_buffer_size = wrapper_get_input_buffer_size;
    adev->set_mode = wrapper_set_mode;
    adev->set_parameters = wrapper_set_parameters;
    adev->dump = wrapper_dump;

    /* Our HAL does not support these methods */
    adev->get_master_volume = NULL;
    adev->set_master_mute = NULL;
    adev->get_master_mute = NULL;
    adev->create_audio_patch = NULL;
    adev->release_audio_patch = NULL;
    adev->get_audio_port = NULL;
    adev->set_audio_port_config = NULL;

    /* Output */
    adev->open_output_stream = wrapper_open_output_stream;
    adev->close_output_stream = wrapper_close_output_stream;

    /* Input */
    adev->open_input_stream = wrapper_open_input_stream;
    adev->close_input_stream = wrapper_close_input_stream;

    return 0;
}

static struct hw_module_methods_t wrapper_module_methods = {
    .open = wrapper_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "MIPAD AUDIO HAL wrapper",
        .author = "arttttt",
        .methods = &wrapper_module_methods,
    },
};
