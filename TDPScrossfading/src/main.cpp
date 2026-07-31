#include <Arduino.h>
#include "driver/i2s.h"

static const i2s_port_t i2s_num = I2S_NUM_0;

// Buffer and audio parameters
const uint16_t sample_count = 512; // Ultra-low latency buffer size
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

// --- Micro-Crossfade State Variables ---
const int FADE_LENGTH = 128; // Crossfade duration in samples (~2.9ms at 44.1kHz)
float fade_ptr = 0.0f;       // Secondary pointer used ONLY during crossfading
int fade_counter = 0;        // Countdown timer for active crossfade
bool is_fading = false;      // Flag indicating if a crossfade is currently active

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
    // 1. Extract active channel (Right Channel)
    int32_t active_sample = (int32_t)(sample_buffer[i] & 0xFFFFFFFF);

    // 2. Write incoming sample into circular buffer
    delay_buffer[write_ptr] = (float)active_sample;

    // 3. True Circular Distance Check
    float dist = fabsf((float)write_ptr - read_ptr1);
    if (dist > (BUFFER_SIZE / 2.0f)) {
      dist = BUFFER_SIZE - dist;
    }

    // Trigger crossfade when pointers get close (within 12 samples)
    if (!is_fading && dist < 12.0f) 
    {
      is_fading = true;
      fade_counter = FADE_LENGTH;

      // DO NOT jump read_ptr1 yet! 
      // Instead, set fade_ptr to a safe target position (e.g. 512 samples away from write_ptr)
      fade_ptr = read_ptr1 + 512.0f;
      if (fade_ptr >= BUFFER_SIZE) {
        fade_ptr -= BUFFER_SIZE;
      }
    }

    // 4. Read Primary Pointer (read_ptr1 stays smooth and uninterrupted!)
    int rp1_floor = (int)read_ptr1;
    int rp1_ceil = (rp1_floor + 1) % BUFFER_SIZE;
    float val_main = delay_buffer[rp1_floor] + (read_ptr1 - rp1_floor) * (delay_buffer[rp1_ceil] - delay_buffer[rp1_floor]);

    float final_sample = val_main;

    // 5. Apply Micro-Crossfade Blend (If Active)
    if (is_fading) 
    {
      // Read Secondary Target Pointer (fade_ptr)
      int fp_floor = (int)fade_ptr;
      int fp_ceil = (fp_floor + 1) % BUFFER_SIZE;
      float val_target = delay_buffer[fp_floor] + (fade_ptr - fp_floor) * (delay_buffer[fp_ceil] - delay_buffer[fp_floor]);

      // Calculate crossfade weights
      // Start at 100% val_main, end at 100% val_target
      float fade_target = (float)(FADE_LENGTH - fade_counter) / (float)FADE_LENGTH;
      float fade_main = 1.0f - fade_target;

      final_sample = (val_main * fade_main) + (val_target * fade_target);

      // Advance target pointer alongside main pointer
      fade_ptr += pitch_ratio;
      if (fade_ptr >= BUFFER_SIZE) {
        fade_ptr -= BUFFER_SIZE;
      }

      fade_counter--;
      
      // When fade completes, silently transfer read_ptr1 to the new safe position!
      if (fade_counter <= 0) {
        read_ptr1 = fade_ptr; 
        is_fading = false; 
      }
    }

    // 6. Output Clamping
    if (final_sample > INT32_MAX) final_sample = INT32_MAX;
    if (final_sample < INT32_MIN) final_sample = INT32_MIN;
    int32_t out_sample = (int32_t)final_sample;

    // 7. Re-pack stereo frame
    sample_buffer[i] = ((uint64_t)(uint32_t)out_sample << 32) | (uint32_t)out_sample;

    // 8. Increment pointers
    write_ptr = (write_ptr + 1) % BUFFER_SIZE;

    read_ptr1 += pitch_ratio;
    if (read_ptr1 >= BUFFER_SIZE) {
      read_ptr1 -= BUFFER_SIZE;
    }
  }
}

void setup()
{
  Serial.begin(115200);

  // Clear delay buffer
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

  static float pitch_ratio = 1.20f; 

  // Read raw audio from mic
  i2s_read(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesRead, portMAX_DELAY);

  // Process DSP
  processAudio(pitch_ratio);

  // Write pitch-shifted audio to DAC
  i2s_write(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesWritten, portMAX_DELAY);
}