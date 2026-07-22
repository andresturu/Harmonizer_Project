#include <Arduino.h>
#include "driver/i2s.h"

// Plays a pure sine tone out over I2S to a MAX98357A (or similar) amp.
// No mic input, no FFT — just a generated sine wave written straight to I2S.

static const i2s_port_t i2s_num = I2S_NUM_0;

const uint16_t sample_count = 1024;          // samples per i2s_write() call
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 2;                  // stereo frame (L+R packed together)
const uint16_t i2s_write_size_bytes = sample_count * i2s_bytes_per_sample * num_channels;

// One 64-bit slot per stereo frame: upper 32 bits = left, lower 32 bits = right
uint64_t sample_buffer[sample_count];

static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),   // TX only, no RX needed
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
    .bck_io_num = 26,     // bit clock -> BCLK on amp
    .ws_io_num = 25,      // word select -> LRCLK on amp
    .data_out_num = 22,   // data out -> DIN on amp
    .data_in_num = I2S_PIN_NO_CHANGE   // not used, no mic input
};

// Tone parameters
const float testTone_freq = 440.0f;             // A4
const float sampleRate = 44100.0f;
const float testTone_amplitude = 20000000.0f; // stays within int32 range; tune to taste
float testTone_phase = 0.0f;

void fillSineBuffer() {
  const float phaseIncrement = 2.0f * PI * testTone_freq / sampleRate;

  for (int i = 0; i < sample_count; i++) {
    float val = testTone_amplitude * sinf(testTone_phase);

    if (val > (float)INT32_MAX) val = (float)INT32_MAX;
    if (val < (float)INT32_MIN) val = (float)INT32_MIN;

    int32_t sample = (int32_t)roundf(val);
    sample_buffer[i] = ((uint64_t)(uint32_t)sample << 32) | (uint32_t)sample; // same on both channels

    testTone_phase += phaseIncrement;
  }

  // keep phase bounded so it doesn't grow unbounded over long runtimes
  if (testTone_phase > 2.0f * PI) {
    testTone_phase -= 2.0f * PI * floorf(testTone_phase / (2.0f * PI));
  }
}

void setup() {
  Serial.begin(115200);

  esp_err_t err = i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install failed: %s (%d)\n", esp_err_to_name(err), err);
  }

  i2s_set_pin(i2s_num, &pin_config);
  Serial.println(i2s_write_size_bytes);
}

void loop() {
  fillSineBuffer();

  size_t bytesWritten;
  i2s_write(i2s_num, sample_buffer, i2s_write_size_bytes, &bytesWritten, portMAX_DELAY);
}