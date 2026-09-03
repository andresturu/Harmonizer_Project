#ifndef VOLUMESCALING
#define VOLUMESCALING

#include <math.h>
#include <cstdint>

//rename to be more specific
const float MAX_SAMPLE_VALUE = 2147483648.0f; // 2^31

const float HP_ALPHA = 0.995f; // closer to 1.0 = lower cutoff frequency
const float EMA_ALPHA = 0.5f; // for exponentional moving average for dB
const float NOISE_FLOOR_DB = -40.0f; // the softest input sound before silent output
const float NOISE_CEILING_DB  = -6.0f; // the loudest input sound before maxing out ouput

//perhaps rename better
float get_scaled_amplitude(int32_t* sample_buffer, int sample_count);



#endif