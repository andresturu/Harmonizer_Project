#include <Arduino.h>
#include "driver/i2s.h"

// Pure passthrough: read from INMP441 mic, write straight to speaker.
// No FFT, no processing at all — isolates whether the static is a hardware
// wiring/config issue or something introduced in software.

static const i2s_port_t i2s_num = I2S_NUM_0;

const uint16_t sample_count = 1024;
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 2;
const uint16_t i2s_bytes = sample_count * i2s_bytes_per_sample * num_channels;

uint64_t sample_buffer[sample_count];

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
    .fixed_mclk = -1
};

static const i2s_pin_config_t pin_config = {
    .bck_io_num = 26,
    .ws_io_num = 25,
    .data_out_num = 22,   // to amp DIN
    .data_in_num = 23     // from mic SD
};

void setup() {
  Serial.begin(115200);

  esp_err_t err = i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install failed: %s (%d)\n", esp_err_to_name(err), err);
  }

  i2s_set_pin(i2s_num, &pin_config);
}

void loop() {
  size_t bytesRead, bytesWritten;

  // 1. Read the raw I2S buffer
  i2s_read(i2s_num, sample_buffer, i2s_bytes, &bytesRead, portMAX_DELAY);

  // Cast buffer to 32-bit signed integers (Left and Right alternate: [L0, R0, L1, R1...])
  int32_t *samples = (int32_t*) sample_buffer;
  size_t total_samples = bytesRead / sizeof(int32_t);

  // 2. Overwrite the noisy Right channel with the clean Left channel
  for (size_t i = 0; i < total_samples; i += 2) {
    int32_t left_sample = samples[i];

    // Optional: Bit-mask the lower 8 bits of unused INMP441 noise
    left_sample &= 0xFFFFFF00; 

    samples[i]     = left_sample; // Left Channel (Clean mic audio)
    samples[i + 1] = left_sample; // Right Channel (Copy of Left channel)
  }

  // 3. Send the clean audio to the PCM5102 DAC
  i2s_write(i2s_num, sample_buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  // size_t bytesRead, bytesWritten;

  // i2s_read(i2s_num, sample_buffer, i2s_bytes, &bytesRead, portMAX_DELAY);
  // i2s_write(i2s_num, sample_buffer, bytesRead, &bytesWritten, portMAX_DELAY);
}