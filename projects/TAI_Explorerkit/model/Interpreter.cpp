#include "model.h"
#include <stdio.h>
#include <math.h>
#include "sl_ml_model_somni_guard.h"
#include "ppg_compress_stat7.h"

#define WINDOW_FRAMES PPG_COMPRESS_STAT7_WINDOW_FRAMES
#define RAW_CHANNELS 28
#define COMPRESSED_CHANNELS PPG_COMPRESS_STAT7_N_OUT_CHANNELS

static const float STAT7_MEAN[10] = {
  -1.0107890367507935f,
  462.87542724609375f,
  -713.2999267578125f,
  734.8062744140625f,
  710.6092529296875f,
  0.09494941681623459f,
  -0.794873833656311f,
  94.79252624511719f,
  86.17684936523438f,
  0.006570133380591869f
};

static const float STAT7_STD[10] = {
  655.80126953125f,
  297.2281494140625f,
  796.1035766601562f,
  708.5175170898438f,
  477.2164001464844f,
  0.6425100564956665f,
  1.0446852445602417f,
  3.6999270915985107f,
  17.4013614654541f,
  0.00964649673551321f
};

static TfLiteTensor* model_input = nullptr;
static TfLiteTensor* model_output = nullptr;

static float in_window[WINDOW_FRAMES * RAW_CHANNELS];
static float out_window[WINDOW_FRAMES * COMPRESSED_CHANNELS];
static int buffer_index = 0;
static bool buffer_full = false;



void init_model() {
    sl_status_t status = sl_ml_model_init(&sl_ml_somni_guard_model_handle);
    if (status != SL_STATUS_OK) {
        printf("Model init failed\r\n");
        return;
    }

    model_input = sl_ml_somni_guard_model_handle.input_tensor(0);
    model_output = sl_ml_somni_guard_model_handle.output_tensor(0);
    
    printf("Model Init OK. Input bytes: %lu, type: %d\r\n", (unsigned long)model_input->bytes, model_input->type);
    
    printf("Input dims: ");
    for(int i=0; i < model_input->dims->size; i++) {
        printf("%d ", model_input->dims->data[i]);
    }
    printf("\r\n");

    printf("Output bytes: %lu, type: %d\r\n", (unsigned long)model_output->bytes, model_output->type);
}

void process_new_frame(const float* frame) {
    for (int i = 0; i < RAW_CHANNELS; i++) {
        in_window[buffer_index * RAW_CHANNELS + i] = frame[i];
    }
    buffer_index++;

    if (buffer_index >= WINDOW_FRAMES) {
        buffer_full = true;
        buffer_index = 0; 
    }

    if (!buffer_full) {
        printf("WAIT\r\n");
        return;
    }

    printf("INF_START\r\n");
    if (model_input == nullptr) {
        printf("ERR:NoModel\r\n");
        return;
    }

    int8_t* input_data = model_input->data.int8;

    if (model_input->bytes != WINDOW_FRAMES * COMPRESSED_CHANNELS) {
        printf("ERR:BadInputSize:%d\r\n", (int)model_input->bytes);
        return;
    }

    // Compress the window
    ppg_compress_stat7_compress_window(in_window, out_window);

    // Normalize and quantize
    for (int t = 0; t < WINDOW_FRAMES; t++) {
        for (int c = 0; c < COMPRESSED_CHANNELS; c++) {
            int idx = t * COMPRESSED_CHANNELS + c;
            float val = out_window[idx];
            
            // Z-score Normalization
            float val_norm = (val - STAT7_MEAN[c]) / STAT7_STD[c];

            // Quantization
            float val_q = round((val_norm / model_input->params.scale) + model_input->params.zero_point);

            // Clamp
            if (val_q > 127) val_q = 127;
            if (val_q < -128) val_q = -128;

            input_data[idx] = (int8_t)val_q;
        }
    }

    printf("INF_PRE_INVOKE\r\n");
    fflush(stdout);

    sl_status_t invoke_status = sl_ml_model_run(&sl_ml_somni_guard_model_handle);
    
    printf("INF_POST_INVOKE\r\n");
    fflush(stdout);

    if (invoke_status != SL_STATUS_OK) {
        printf("ERR:InvokeFail\r\n");
        fflush(stdout);
        return;
    }

    float prob = 0.0f;
    if (model_output->type == 1 /* kTfLiteFloat32 */) {
        prob = model_output->data.f[0];
    } else {
        int8_t output_q = model_output->data.int8[0];
        prob = (output_q - model_output->params.zero_point) * model_output->params.scale;
    }

    int pred = (prob >= 0.5f) ? 1 : 0;

    printf("PRED:%d\r\n", pred);
    printf("CONF:%.4f\r\n", prob);
    fflush(stdout);
}

void reset_buffer() {
    buffer_index = 0;
    buffer_full = false;
    printf("RESET_OK\r\n");
    fflush(stdout);
}