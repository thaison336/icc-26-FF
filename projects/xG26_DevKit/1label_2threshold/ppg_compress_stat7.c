#include "ppg_compress_stat7.h"
#include <math.h>

void ppg_compress_stat7_compress_frame(const float *x, int n, float *out) {
    int oi = 0;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += x[i];
    float mean_v = sum / (float)n;
    float sumsq_dev = 0.0f;
    for (int i = 0; i < n; i++) { float d = x[i] - mean_v; sumsq_dev += d * d; }
    float std_v = sqrtf(sumsq_dev / (float)n);
    float min_v = x[0]; for (int i = 1; i < n; i++) if (x[i] < min_v) min_v = x[i];
    float max_v = x[0]; for (int i = 1; i < n; i++) if (x[i] > max_v) max_v = x[i];

    out[oi++] = mean_v;
    out[oi++] = std_v;
    out[oi++] = min_v;
    out[oi++] = max_v;
    { float sumsq = 0.0f; for (int i = 0; i < n; i++) sumsq += x[i] * x[i]; out[oi++] = sqrtf(sumsq / (float)n); }
    { float s3 = 0.0f; float sd = (std_v < 1e-8f) ? 1.0f : std_v; for (int i = 0; i < n; i++) { float z = (x[i] - mean_v) / sd; s3 += z * z * z; } out[oi++] = s3 / (float)n; }
    { float s4 = 0.0f; float sd = (std_v < 1e-8f) ? 1.0f : std_v; for (int i = 0; i < n; i++) { float z = (x[i] - mean_v) / sd; s4 += z * z * z * z; } out[oi++] = (s4 / (float)n) - 3.0f; }
}

void ppg_compress_stat7_compress_window(const float *in_window, float *out_window) {
    const int in_stride = 28;
    const int out_stride = 10;
    for (int t = 0; t < 60; t++) {
        const float *row = in_window + t * in_stride;
        float spo2_v = row[0];
        float bpm_v  = row[1];
        const float *ir = row + 2;
        float motion_v = row[27];
        float *out_row = out_window + t * out_stride;
        ppg_compress_stat7_compress_frame(ir, 25, out_row);
        out_row[7]     = spo2_v;
        out_row[8] = bpm_v;
        out_row[9] = motion_v;
    }
}
