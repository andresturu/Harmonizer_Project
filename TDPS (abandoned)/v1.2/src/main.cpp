#include <Arduino.h>
#include "driver/i2s.h"

static const i2s_port_t i2s_num = I2S_NUM_0;

const uint16_t sample_count = 512;
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 2;
const uint16_t i2s_read_size_bytes = sample_count * i2s_bytes_per_sample * num_channels;

uint64_t sample_buffer[sample_count];

#define BUFFER_SIZE 2048
float delay_buffer[BUFFER_SIZE];
int write_ptr = 0;

float read_ptr1 = 0.0f;
float read_ptr2 = BUFFER_SIZE / 2;

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
    .bck_io_num = 26,
    .ws_io_num = 25,
    .data_out_num = 22,
    .data_in_num = 23};

int getButton()
{
  int button;
  return button;
}

enum Interval
{
  tonic,
  minor_third,
  major_third,
  perfect_fourth,
  perfect_fifth
};

float updateInterval(int button_pressed)
{
  switch (button_pressed)
  {
  case tonic:
    return 1.0f;
  case minor_third:
    return 6.0f / 5;
  case major_third:
    return 1.25f;
  case perfect_fourth:
    return 4.0f / 3;
  case perfect_fifth:
    return 1.5f;
  default:
    return 1.0f;
  }
}

void processAudio(float pitch_ratio)
{
  for (int i = 0; i < sample_count; i++)
  {
    int32_t right_sample = (int32_t)(sample_buffer[i] & 0xFFFFFFFF);
    int32_t active_sample = right_sample;

    delay_buffer[write_ptr] = (float)active_sample;

    float dist1 = write_ptr - read_ptr1;
    if (dist1 < 0)
      dist1 += BUFFER_SIZE;

    // Triangle window: 0 at each tap's own collision, 1 at its own midpoint.
    float fade1 = 1.0f - fabsf(2.0f * dist1 / (float)BUFFER_SIZE - 1.0f);
    float fade2 = 1.0f - fade1;

    int rp1_floor = (int)read_ptr1;
    int rp2_floor = (int)read_ptr2;
    int rp1_ceil = (rp1_floor + 1) % BUFFER_SIZE;
    int rp2_ceil = (rp2_floor + 1) % BUFFER_SIZE;

    float val1 = delay_buffer[rp1_floor] + (read_ptr1 - rp1_floor) * (delay_buffer[rp1_ceil] - delay_buffer[rp1_floor]);
    float val2 = delay_buffer[rp2_floor] + (read_ptr2 - rp2_floor) * (delay_buffer[rp2_ceil] - delay_buffer[rp2_floor]);

    float shifted_signal = (val1 * fade1) + (val2 * fade2);

    if (shifted_signal > INT32_MAX)
      shifted_signal = INT32_MAX;
    if (shifted_signal < INT32_MIN)
      shifted_signal = INT32_MIN;
    int32_t out_sample = (int32_t)shifted_signal;

    sample_buffer[i] = ((uint64_t)(uint32_t)out_sample << 32) | (uint32_t)out_sample;

    write_ptr = (write_ptr + 1) % BUFFER_SIZE;

    read_ptr1 += pitch_ratio;
    if (read_ptr1 >= BUFFER_SIZE)
      read_ptr1 -= BUFFER_SIZE;

    read_ptr2 += pitch_ratio;
    if (read_ptr2 >= BUFFER_SIZE)
      read_ptr2 -= BUFFER_SIZE;
  }
}

void setup()
{
  Serial.begin(115200);

  for (int i = 0; i < BUFFER_SIZE; i++)
  {
    delay_buffer[i] = 0.0f;
  }

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

  i2s_read(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesRead, portMAX_DELAY);

  pitch_ratio = 1.25f;
  processAudio(pitch_ratio);

  i2s_write(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesWritten, portMAX_DELAY);
}