#ifndef MODEL_H_
#define MODEL_H_

extern const unsigned char model_int8_tflite[];
extern const unsigned int model_int8_tflite_len;

#ifdef __cplusplus
extern "C" {
#endif

void init_model();
void process_new_frame(const float* frame);
void reset_buffer();

#ifdef __cplusplus
}
#endif

#endif // MODEL_H_
