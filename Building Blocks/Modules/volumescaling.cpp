#include "volumescaling.h"

void high_pass_filter(int32_t* sample_buffer, int sample_count);
float get_rms(int32_t* sample_buffer, int sample_count);
float rms_to_db(float rms);
float smooth_dB(float old_dB, float new_dB);
float dB_to_gain(float dB);
float clamp(float value, float low, float high);

float get_scaled_amplitude(int32_t* sample_buffer, int sample_count) {
    
    high_pass_filter(sample_buffer, sample_count);
    
    //calculate RMS, dB, smooth dB with exponential moving average, calculate output_gain
    //output_gain is multiplied against the output sound buffer to achieve logarithmic/human volume adaptation
    float rms = get_rms(sample_buffer, sample_count);
    float dB = rms_to_db(rms);
    static float smoothed_dB = dB;
    smoothed_dB = smooth_dB(smoothed_dB, dB);
    float output_gain = dB_to_gain(smoothed_dB);
    
    return output_gain;
}



void high_pass_filter(int32_t* sample_buffer, int sample_count) {
    static float hp_prev_input = 0.0f;
    static float hp_prev_output = 0.0f;
    for (int i = 0; i < sample_count; i++) {
        float x = (float)sample_buffer[i];
        float y = HP_ALPHA * (hp_prev_output + x - hp_prev_input);
        hp_prev_input = x;
        hp_prev_output = y;
        sample_buffer[i] = (int32_t)y;
    }
}

float get_rms(int32_t* sample_buffer, int sample_count) {
  float sum = 0.0f;
  for (int i = 0; i<sample_count; i++) {
    float normalized_val = ((float)sample_buffer[i])/MAX_SAMPLE_VALUE;  
    sum+= normalized_val * normalized_val;
  }
  float average = sum / sample_count;
  float rms = sqrtf(average);
  return rms;
}

float rms_to_db(float rms) {
  const float epsilon = 1e-9f;
  return 20.0f * log10f(fmaxf(rms, epsilon));
}

float smooth_dB(float old_dB, float new_dB) {
  return old_dB*(1.0f-EMA_ALPHA) + new_dB*(EMA_ALPHA);
}

float dB_to_gain(float dB) {
  float normalized = (dB - NOISE_FLOOR_DB) / (NOISE_CEILING_DB - NOISE_FLOOR_DB);
  return clamp(normalized, 0.0f, 1.0f);
}

float clamp(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}