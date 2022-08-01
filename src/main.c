#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <spa/param/latency-utils.h>
#include <spa/pod/builder.h>

#include <pipewire/filter.h>
#include <pipewire/pipewire.h>

#include <nvAudioEffects.h>

#include "circlebuf.h"

#define DEFAULT_RATE 48000
#define DEFAULT_CHANNELS 1
#define NV_INTENSITY_RATIO 1.0
#define NV_SAMPLES_PER_FRAME 480 // You can't change this, NvAFX won't allow it atm
#define NV_SAMPLE_SIZE NV_SAMPLES_PER_FRAME * sizeof(float)

struct data {
    struct pw_main_loop *loop;
    struct pw_filter *filter;
    struct port *in_port;
    struct port *out_port;

    NvAFX_Handle nv_handle;
    unsigned int num_channels;
    unsigned int num_samples;

    float input_data[NV_SAMPLE_SIZE * 2];
    float output_data[NV_SAMPLE_SIZE * 2];
    struct circlebuf input_buffer;
    struct circlebuf output_buffer;

    char *model_path;
};

struct port {
    struct data *data;
};

int nvafx_init(struct data *data) {
    NvAFX_Status err = NvAFX_InitializeLogger(LOG_LEVEL_INFO, 1, "", NULL, NULL);
    if (err != NVAFX_STATUS_SUCCESS) {
        pw_log_error("NvAFX_InitializeLogger(%d, %d, %s, %p, %p) failed: %d\n", LOG_LEVEL_INFO, 1, "", NULL, NULL, err);
        return err;
    }
    if (data->nv_handle == NULL) {
        err = NvAFX_CreateEffect(NVAFX_EFFECT_DENOISER, &data->nv_handle);
        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("NvAFX_CreateEffect(%s, %p) failed: %d\n", NVAFX_EFFECT_DENOISER, data->nv_handle, err);
            return err;
        }

        err = NvAFX_SetString(data->nv_handle, NVAFX_PARAM_DENOISER_MODEL_PATH, data->model_path);
        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("NvAFX_SetString(%p, %s, '%s') failed: %d\n", data->nv_handle, NVAFX_PARAM_DENOISER_MODEL_PATH,
                         data->model_path, err);
            return err;
        }

        err = NvAFX_SetU32(data->nv_handle, NVAFX_PARAM_INPUT_SAMPLE_RATE, DEFAULT_RATE);
        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("NvAFX_SetU32(%p, %s, %d) failed: %d\n", data->nv_handle, NVAFX_PARAM_INPUT_SAMPLE_RATE,
                         DEFAULT_RATE, err);
            return err;
        }

        err = NvAFX_SetU32(data->nv_handle, NVAFX_PARAM_NUM_SAMPLES_PER_FRAME, NV_SAMPLES_PER_FRAME);
        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("NvAFX_SetU32(%p, %s, %d) failed: %d\n", data->nv_handle, NVAFX_PARAM_INPUT_SAMPLE_RATE,
                         NV_SAMPLES_PER_FRAME, err);
            return err;
        }

        err = NvAFX_SetFloat(data->nv_handle, NVAFX_PARAM_INTENSITY_RATIO, NV_INTENSITY_RATIO);
        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("NvAFX_SetU32(%p, %s, %f) failed: %d\n", data->nv_handle, NVAFX_PARAM_INTENSITY_RATIO,
                         NV_INTENSITY_RATIO, err);
            return err;
        }

        err = NvAFX_Load(data->nv_handle);
        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("NvAFX_Load(%p) failed: %d\n", data->nv_handle, err);
            return err;
        }

        err = NvAFX_GetU32(data->nv_handle, NVAFX_PARAM_NUM_CHANNELS, &data->num_channels);
        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("NvAFX_GetU32(%p, %s, %p) failed: %d\n", data->nv_handle, NVAFX_PARAM_NUM_CHANNELS,
                         &data->num_channels, err);
            return err;
        }

        err = NvAFX_GetU32(data->nv_handle, NVAFX_PARAM_NUM_SAMPLES_PER_FRAME, &data->num_samples);
        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("NvAFX_GetU32(%p, %s, %p) failed: %d\n", data->nv_handle, NVAFX_PARAM_NUM_SAMPLES_PER_FRAME,
                         &data->num_samples, err);
            return err;
        }
    }

    return 0;
}

static void on_process(void *userdata, struct spa_io_position *position) {
    struct data *data = userdata;
    uint32_t n_samples = position->clock.duration;

    size_t n_samples_size = n_samples * sizeof(float);

    float *in = pw_filter_get_dsp_buffer(data->in_port, n_samples);
    float *out = pw_filter_get_dsp_buffer(data->out_port, n_samples);

    if (in == NULL || out == NULL || data->nv_handle == NULL) {
        return;
    }
    if (n_samples != data->num_samples) {
        circlebuf_push(&data->input_buffer, in, n_samples_size);
        if (data->input_buffer.size >= NV_SAMPLE_SIZE) {
            circlebuf_pop(&data->input_buffer, in, NV_SAMPLE_SIZE);

            NvAFX_Status err =
                NvAFX_Run(data->nv_handle, (const float **)&in, &in, data->num_samples, data->num_channels);

            if (err != NVAFX_STATUS_SUCCESS) {
                pw_log_error("nvafx_run(%p, %p, %p, %d, %d) failed: %d", data->nv_handle, in, in, data->num_samples,
                             data->num_channels, err);
                return;
            }
            circlebuf_push(&data->output_buffer, in, NV_SAMPLE_SIZE);
        }

        circlebuf_pop(&data->output_buffer, out, n_samples_size);
    } else {
        NvAFX_Status err = NvAFX_Run(data->nv_handle, (const float **)&in, &out, data->num_samples, data->num_channels);

        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("nvafx_run(%p, %p, %p, %d, %d) failed: %d", data->nv_handle, in, out, data->num_samples,
                         data->num_channels, err);
            return;
        }
    }
}

static void on_statechanged(void *userdata, enum pw_filter_state old, enum pw_filter_state state, const char *error) {
    struct data *data = userdata;
    switch (state) {
    case PW_FILTER_STATE_UNCONNECTED:
        circlebuf_init(&data->input_buffer, &data->input_data, NV_SAMPLE_SIZE * 2);
        circlebuf_init(&data->output_buffer, &data->output_data, NV_SAMPLE_SIZE * 2);
        pw_log_info("plugin %p: unconnected", data);
        break;
    case PW_FILTER_STATE_ERROR:
        pw_log_info("plugin %p: error: %s", data, error);
        break;
    default:
        break;
    }
}

static const struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .process = on_process,
    .state_changed = on_statechanged,
};

static void do_quit(void *userdata, int signal_number) {
    struct data *data = userdata;
    if (data->nv_handle != NULL) {
        NvAFX_Status err = NvAFX_DestroyEffect(data->nv_handle);
        if (err != NVAFX_STATUS_SUCCESS) {
            pw_log_error("NvAFX_DestroyEffect(%p) failed: %d\n", data->nv_handle, err);
        }
        data->nv_handle = NULL;
    }

    circlebuf_free(&data->input_buffer);
    circlebuf_free(&data->output_buffer);

    pw_main_loop_quit(data->loop);
}

int main(int argc, char *argv[]) {
    struct data data = {
        0,
    };

    if (argc == 0) {
        fprintf(stderr, "usage: %s <model>\n", argv[0]);
        return -1;
    }

    data.model_path = argv[1];

    if (access(data.model_path, F_OK) != 0) {
        fprintf(stderr, "model: %s does not exist\n", data.model_path);
        return -1;
    }

    if (nvafx_init(&data) != 0) {
        fprintf(stderr, "can't initialize nvafx\n");
        return -1;
    }

    circlebuf_init(&data.input_buffer, &data.input_data, NV_SAMPLE_SIZE * 2);
    circlebuf_init(&data.output_buffer, &data.output_data, NV_SAMPLE_SIZE * 2);

    const struct spa_pod *params[2];
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    pw_init(&argc, &argv);

    pw_log_set_level(SPA_LOG_LEVEL_INFO);
    data.loop = pw_main_loop_new(NULL);

    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT, do_quit, &data);
    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM, do_quit, &data);

    data.filter =
        pw_filter_new_simple(pw_main_loop_get_loop(data.loop), "NVIDIA Denoiser",
                             pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Source",
                                               PW_KEY_MEDIA_ROLE, "DSP", PW_KEY_MEDIA_CLASS, "Audio/Source",
                                               PW_KEY_NODE_RATE, "1/48000", PW_KEY_NODE_FORCE_QUANTUM, "480", NULL),
                             &filter_events, &data);

    data.in_port = pw_filter_add_port(
        data.filter, PW_DIRECTION_INPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(struct port),
        pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio", PW_KEY_PORT_NAME, "input", NULL), NULL, 0);

    data.out_port = pw_filter_add_port(
        data.filter, PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(struct port),
        pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio", PW_KEY_PORT_NAME, "output", NULL), NULL, 0);

    params[0] = spa_process_latency_build(&b, SPA_PARAM_ProcessLatency,
                                          &SPA_PROCESS_LATENCY_INFO_INIT(.ns = 20 * SPA_NSEC_PER_MSEC));
    params[1] =
        spa_process_latency_build(&b, SPA_PARAM_Latency, &SPA_PROCESS_LATENCY_INFO_INIT(.ns = 20 * SPA_NSEC_PER_MSEC));

    if (pw_filter_connect(data.filter, PW_FILTER_FLAG_RT_PROCESS, params, 1) < 0) {
        fprintf(stderr, "can't connect\n");
        return -1;
    }

    pw_main_loop_run(data.loop);
    pw_filter_destroy(data.filter);
    pw_main_loop_destroy(data.loop);
    pw_deinit();

    return 0;
}
