#include <Arduino.h>
#include "driver/i2s.h"
#include <arduinoFFT.h>

static const i2s_port_t i2s_num = I2S_NUM_0; // i2s port number

// how many samples to take per i2s_read()
const uint16_t sample_count = 1024;

// i2s constants, remember using both channels -> hence multiplying by num_channels == 2
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 2;
const uint16_t i2s_read_size_bytes = sample_count * i2s_bytes_per_sample * num_channels;

// i2s buffer variable, holds raw i2S data from INMP441
// raw i2S data is simply voltages! Audio is just voltages
uint64_t sample_buffer[sample_count];

static const i2s_config_t i2s_config = {
    // master means esp32 generates clock signals, acts as both transmitter and receiver
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = 44100, // standard CD quality sample rate
    // uses 32 bits to snapshot of one sample at one moment of time, 16 bits is standard for CD quality audio
    .bits_per_sample = i2s_bits_per_sample, // says each individual channel's sample is 32 bits wide

    // stereo two channel format (one 32-bit sample for left, one 32-bit for right, packed into 64 bit)
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority

    // honestly don't know what these mean, might have to investigate these more
    .dma_buf_count = 8,
    .dma_buf_len = 256, // 1024,
    .use_apll = 0,
    .tx_desc_auto_clear = true,
    .fixed_mclk = -1};

// pin configurations
static const i2s_pin_config_t pin_config = {
    .bck_io_num = 26,   // The bit clock connectiom, goes to pin 26 of ESP32
    .ws_io_num = 25,    // Word select, also known as word select or left right clock
    .data_out_num = 22, // Data out from the ESP32, connect to DIN on 38357A
    .data_in_num = 23   // data in from INMP441
};

// QueueHandle_t i2s_queue = nullptr; // deals with event notifications -> optional debugging tool

/*
These are the input and output vectors
Input vectors receive computed results from FFT
*/
float vReal[sample_count];
float vImag[sample_count];

/* Create FFT object */
ArduinoFFT<float> FFT = ArduinoFFT<float>(vReal, vImag, sample_count, 44100);

const float music_interval = 1.5f;

/* function that does nothing with the audio
void processAudio() {
  for (int i = 0; i < sample_count; i++) {
    // 1. Extract the active microphone channel
    int32_t left_sample  = (int32_t)(sample_buffer[i] >> 32);
    int32_t right_sample = (int32_t)(sample_buffer[i] & 0xFFFFFFFF);
    int32_t active_sample = (left_sample != 0) ? left_sample : right_sample;

    // 2. Pass it straight to the output (no FFT, no math)
    sample_buffer[i] = ((uint64_t)(uint32_t)active_sample << 32) | (uint32_t)active_sample;
  }
}
*/

// take sample_buffer, apply FFT, and produce array t
void processAudio()
{

  // put 1024 values of 32 bits (left channel) into vReal
  //  make every value in vImag[i] = 0.0
  for (int i = 0; i < sample_count; i++)
  {
    int32_t sample = (int32_t)(sample_buffer[i] & 0xFFFFFFFF); // try LOWER 32 bits instead
    // int32_t sample = (int32_t) (sample_buffer[i] >> 32); //extracts the left channel, and casts it as int32_t
    vReal[i] = (float)sample; // casts 32_bit into double type for vReal
    vImag[i] = 0.0f;
  }

  // found that windowing makes audio worse
  // multiplies array by smoothing curve, basically giving less weight to edge frequencies, which can change abruptly after bins change suddenly
  // FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);

  // // stores complex values in vReal and vImag for each "bin", note that FFt.compute() changes vReal and vImag automatically
  // // a "bin" holds a complex number representing amplitude and phase of a specific frequency in the signal
  // //something to note is symmetry: Real[k] = Real[N-k] and Imag[k] = -Imag[N-k] *could be a future optimization
  // takes raw voltages from sample_buffer and figures out the amplitudes and frequencies
  FFT.compute(FFTDirection::Forward); /* Compute FFT */

  // helper arrays
  static float temp_real[sample_count];
  static float temp_imag[sample_count];

  // zero-init: needed so unfilled (gap) bins stay silent, not garbage
  for (int i = 0; i < sample_count; i++)
  {
    temp_real[i] = 0.0f;
    temp_imag[i] = 0.0f;
  }

  // // because of complex conjugate symmetry only loop over half of sample_count
  for (int i = 0; i < sample_count / 2; i++)
  {
    int new_bin_index = (int)round(i * music_interval);

    // if in range of sample_count/2 //maybe this is wrong?
    if (new_bin_index <= sample_count / 2)
    {
      temp_real[new_bin_index] = vReal[i];
      temp_imag[new_bin_index] = vImag[i];
    }
  }

  // //mirror upper half as complex conjugates
  for (int i = 1; i < sample_count / 2; i++)
  {
    temp_real[sample_count - i] = temp_real[i];
    temp_imag[sample_count - i] = -temp_imag[i];
  }

  // put shifted values into vReal and vImag
  for (int i = 0; i < sample_count; i++)
  {
    vReal[i] = temp_real[i];
    vImag[i] = temp_imag[i];
  }

  // converts frequency-based signal into time-based signal, stored in vReal (now vImag doesn't mean anything)
  FFT.compute(FFTDirection::Reverse);

  // put values from vReal (doubles) into a channel of sample_buffer (unkown if R or L channel)
  // sample_buffer holds 64 bits per element
  for (int i = 0; i < sample_count; i++)
  {
    float val = vReal[i]; // / sample_count; // check ArduinoFFT docs — may need val /= sample_count;
    if (val > INT32_MAX)
      val = INT32_MAX;
    if (val < INT32_MIN)
      val = INT32_MIN;
    int32_t sample = (int32_t)roundf(val);
    sample_buffer[i] = ((uint64_t)(uint32_t)sample << 32) | (uint32_t)sample;
  }
}

void setup()
{

  Serial.begin(115200);

  // check for if driver_install succeeded
  esp_err_t err = i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
  if (err != ESP_OK)
  {
    Serial.printf("i2s_driver_install failed: %s (%d)\n", esp_err_to_name(err), err);
  }
  i2s_set_pin(i2s_num, &pin_config);
  Serial.println(i2s_read_size_bytes);
}

void loop()
{

  // get data from INMP441 mic
  size_t BytesRead;
  // i2s_read only stops once the raw_sample array is full -> &sample_buffer gives pointer, and i2s_read_size_bytes tells the size
  i2s_read(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesRead, portMAX_DELAY);

  // process data with fft before outputting below...
  processAudio();

  // write data to max98357 DAC/amp
  size_t BytesWritten;
  i2s_write(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesWritten, portMAX_DELAY);
}