#include "model.h"
#include <stdio.h>
#include <math.h>
#include "sl_ml_model_somni_guard_v2.h"
#include "ppg_compress_stat7.h"

#define WINDOW_FRAMES PPG_COMPRESS_STAT7_WINDOW_FRAMES
#define RAW_CHANNELS 28
#define COMPRESSED_CHANNELS PPG_COMPRESS_STAT7_N_OUT_CHANNELS

static const float STAT7_MEAN[10] = {
    -9.08638412511209e-06f,
    0.0030737612396478653f,
    -0.00475749047473073f,
    0.004848856944590807f,
    0.004867485724389553f,
    0.05842871591448784f,
    -0.7696008682250977f,
    94.3064193725586f,
    83.89511108398438f,
    0.0058854068629443645f};

static const float STAT7_STD[10] = {
    0.004771340172737837f,
    0.0023004955146461725f,
    0.005945245269685984f,
    0.005335461813956499f,
    0.003716951236128807f,
    0.6678735017776489f,
    1.0906718969345093f,
    5.015106678009033f,
    19.705257415771484f,
    0.009256711229681969f};

static TfLiteTensor *model_input = nullptr;
static TfLiteTensor *model_output = nullptr;

static float in_window[WINDOW_FRAMES * RAW_CHANNELS];
static float out_window[WINDOW_FRAMES * COMPRESSED_CHANNELS];
static int buffer_index = 0;
static bool buffer_full = false;

void init_model()
{
    sl_status_t status = sl_ml_model_init(&sl_ml_somni_guard_v2_model_handle);
    if (status != SL_STATUS_OK)
    {
        printf("Model init failed\r\n");
        return;
    }

    model_input = sl_ml_somni_guard_v2_model_handle.input_tensor(0);
    model_output = sl_ml_somni_guard_v2_model_handle.output_tensor(0);

    printf("Model Init OK. Input bytes: %lu, type: %d\r\n", (unsigned long)model_input->bytes, model_input->type);

    printf("Input dims: ");
    for (int i = 0; i < model_input->dims->size; i++)
    {
        printf("%d ", model_input->dims->data[i]);
    }
    printf("\r\n");

    printf("Output bytes: %lu, type: %d\r\n", (unsigned long)model_output->bytes, model_output->type);
}

void process_new_frame(const float *frame)
{
    for (int i = 0; i < RAW_CHANNELS; i++)
    {
        in_window[buffer_index * RAW_CHANNELS + i] = frame[i];
    }
    buffer_index++;

    if (buffer_index >= WINDOW_FRAMES)
    {
        buffer_full = true;
        buffer_index = 0;
    }

    if (!buffer_full)
    {
        printf("WAIT\r\n");
        return;
    }

    printf("INF_START\r\n");
    if (model_input == nullptr)
    {
        printf("ERR:NoModel\r\n");
        return;
    }

    int8_t *input_data = model_input->data.int8;

    if (model_input->bytes != WINDOW_FRAMES * COMPRESSED_CHANNELS)
    {
        printf("ERR:BadInputSize:%d\r\n", (int)model_input->bytes);
        return;
    }

    // Compress the window
    ppg_compress_stat7_compress_window(in_window, out_window);

    // Normalize and quantize
    for (int t = 0; t < WINDOW_FRAMES; t++)
    {
        for (int c = 0; c < COMPRESSED_CHANNELS; c++)
        {
            int idx = t * COMPRESSED_CHANNELS + c;
            float val = out_window[idx];

            // Z-score Normalization
            float val_norm = (val - STAT7_MEAN[c]) / STAT7_STD[c];

            // Quantization
            float val_q = round((val_norm / model_input->params.scale) + model_input->params.zero_point);

            // Clamp
            if (val_q > 127)
                val_q = 127;
            if (val_q < -128)
                val_q = -128;

            input_data[idx] = (int8_t)val_q;
        }
    }

    printf("INF_PRE_INVOKE\r\n");
    fflush(stdout);

    sl_status_t invoke_status = sl_ml_model_run(&sl_ml_somni_guard_v2_model_handle);

    printf("INF_POST_INVOKE\r\n");
    fflush(stdout);

    if (invoke_status != SL_STATUS_OK)
    {
        printf("ERR:InvokeFail\r\n");
        fflush(stdout);
        return;
    }

    float prob = 0.0f;
    if (model_output->type == 1 /* kTfLiteFloat32 */)
    {
        prob = model_output->data.f[0];
    }
    else
    {
        int8_t output_q = model_output->data.int8[0];
        prob = (output_q - model_output->params.zero_point) * model_output->params.scale;
    }

    int pred = 0;
    if (prob < 0.2783660571179704f)
    {
        pred = 0; // Normal
    }
    else if (prob < 0.862608100601177f)
    {
        pred = 1; // Abnormal
    }
    else
    {
        pred = 2; // Emergency
    }

    printf("PRED:%d\r\n", pred);
    printf("CONF:%.4f\r\n", prob);
    fflush(stdout);
}

void reset_buffer()
{
    buffer_index = 0;
    buffer_full = false;
    printf("RESET_OK\r\n");
    fflush(stdout);
}

int8_t predict_window_confidence(const float *window_60x28)
{
    if (model_input == nullptr || model_output == nullptr || window_60x28 == nullptr)
    {
        printf("[AI ERR] Model or input tensor not initialized\r\n");
        return -1;
    }

    if (model_input->bytes != WINDOW_FRAMES * COMPRESSED_CHANNELS)
    {
        printf("[AI ERR] Bad input tensor size: %lu (expected %d)\r\n",
               (unsigned long)model_input->bytes, WINDOW_FRAMES * COMPRESSED_CHANNELS);
        return -1;
    }

    // 1. Compress 60x28 raw window into 60x10 feature window
    ppg_compress_stat7_compress_window(window_60x28, out_window);

    // 2. Z-Score Normalize and Quantize to int8_t
    int8_t *input_data = model_input->data.int8;
    for (int t = 0; t < WINDOW_FRAMES; t++)
    {
        for (int c = 0; c < COMPRESSED_CHANNELS; c++)
        {
            int idx = t * COMPRESSED_CHANNELS + c;
            float val = out_window[idx];

            // Z-score Normalization
            float val_norm = (val - STAT7_MEAN[c]) / STAT7_STD[c];

            // Quantization
            float val_q = roundf((val_norm / model_input->params.scale) + model_input->params.zero_point);

            // Clamp int8 range [-128, 127]
            if (val_q > 127.0f)
                val_q = 127.0f;
            if (val_q < -128.0f)
                val_q = -128.0f;

            input_data[idx] = (int8_t)val_q;
        }
    }

    // 3. Run TFLite Micro Model Inference
    sl_status_t invoke_status = sl_ml_model_run(&sl_ml_somni_guard_v2_model_handle);
    if (invoke_status != SL_STATUS_OK)
    {
        printf("[AI ERR] sl_ml_model_run failed with status 0x%04X\r\n", (unsigned int)invoke_status);
        fflush(stdout);
        return -1;
    }

    // 4. Extract probability output
    float prob = 0.0f;
    if (model_output->type == 1 /* kTfLiteFloat32 */)
    {
        prob = model_output->data.f[0];
    }
    else
    {
        int8_t output_q = model_output->data.int8[0];
        prob = (output_q - model_output->params.zero_point) * model_output->params.scale;
    }

    uint8_t pred = 0;
    if (prob < 0.2783660571179704f)
    {
        pred = 0; // Normal
    }
    else if (prob < 0.862608100601177f)
    {
        pred = 1; // Abnormal
    }
    else
    {
        pred = 2; // Emergency
    }
    printf("[AI INF] Inference OK -> Pred: %d | Confidence: %.4f\r\n", pred, prob);
    fflush(stdout);

    return pred;
}