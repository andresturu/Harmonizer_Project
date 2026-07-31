#include <Arduino.h>
#include "driver/i2s.h"

static const i2s_port_t i2s_num = I2S_NUM_0;

// Buffer and audio parameters
const uint16_t sample_count = 512; // Dropped to 512 for ultra-low latency
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 2;
const uint16_t i2s_read_size_bytes = sample_count * i2s_bytes_per_sample * num_channels;

// Main I2S DMA buffer
uint64_t sample_buffer[sample_count];

// --- Delay Line Configurations ---
#define BUFFER_SIZE 2048 // Size of the circular delay line
float delay_buffer[BUFFER_SIZE];
int write_ptr = 0;

float read_ptr1 = 0.0f;

// --- I2S Configuration Block ---
static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = i2s_bits_per_sample,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = 0,
    .tx_desc_auto_clear = true,
    .fixed_mclk = -1};

static const i2s_pin_config_t pin_config = {
    .bck_io_num = 26,   // Bit Clock (SCK on Mic, BCLK on DAC)
    .ws_io_num = 25,    // Word Select (WS on Mic, LRC on DAC)
    .data_out_num = 22, // Data out to MAX98357A (DIN)
    .data_in_num = 23   // Data in from INMP441 (SD)
};

// --- Real-time DSP Pitch Shifter ---
void processAudio(float pitch_ratio)
{
  for (int i = 0; i < sample_count; i++)
  {
    //
    // 1. Extract active channel from the 64-bit stereo frame
    // int32_t left_sample = (int32_t)(sample_buffer[i] >> 32);
    int32_t right_sample = (int32_t)(sample_buffer[i] & 0xFFFFFFFF);
    int32_t active_sample = right_sample;
    // int32_t active_sample = (left_sample != 0) ? left_sample : right_sample;

    // 2. Write raw microphone data into circular buffer
    delay_buffer[write_ptr] = (float)active_sample;

    // 5. Read from pointer using linear interpolation
    int rp1_floor = (int)read_ptr1;
    int rp1_ceil = (rp1_floor + 1) % BUFFER_SIZE; // modulus to prevent out of bounds indexing

    // linear interpolation: for estimating values "in between" integer indices
    float val1 = delay_buffer[rp1_floor] + (read_ptr1 - rp1_floor) * (delay_buffer[rp1_ceil] - delay_buffer[rp1_floor]);
   
    // 6. Blend both read heads together using crossfade weights
    float shifted_signal = val1;  //(val1 * fade1) + (val2 * fade2);

    // 7. Clamp outputs to prevent digital clipping
    if (shifted_signal > INT32_MAX)
      shifted_signal = INT32_MAX;
    if (shifted_signal < INT32_MIN)
      shifted_signal = INT32_MIN;
    int32_t out_sample = (int32_t)shifted_signal;

    // 8. Re-pack mono signal into dual-channel stereo output, both L and R channels playing same thing
    sample_buffer[i] = ((uint64_t)(uint32_t)out_sample << 32) | (uint32_t)out_sample;

    // 9. Increment pointers
    write_ptr = (write_ptr + 1) % BUFFER_SIZE;

    read_ptr1 += pitch_ratio;
    if (read_ptr1 >= BUFFER_SIZE)
      read_ptr1 -= BUFFER_SIZE;

  }
}

void setup()
{
  Serial.begin(115200);

  // Initialize delay buffer
  for (int i = 0; i < BUFFER_SIZE; i++)
  {
    delay_buffer[i] = 0.0f;
  }

  // Install and start I2S driver
  esp_err_t err = i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
  if (err != ESP_OK)
  {
    Serial.printf("I2S driver install failed: %s (%d)\n", esp_err_to_name(err), err);
  }
  i2s_set_pin(i2s_num, &pin_config);
  Serial.println("I2S Pitch Shifter initialized successfully.");
}

void loop()
{
  size_t BytesRead;
  size_t BytesWritten;

  static int button_pressed;
  static float pitch_ratio;

  // Read raw audio from mic
  i2s_read(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesRead, portMAX_DELAY);

  pitch_ratio = 1.2f;
  // Process and shift the audio (Time-Domain)
  processAudio(pitch_ratio);

  // Write shifted audio to DAC/Speaker
  i2s_write(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesWritten, portMAX_DELAY);
}