#include <Arduino.h>
#include "driver/i2s.h"

// version 2.1 introduces ability to play chords, and mix which note contributes the most
//  also includes tonic note in the speaker output, unlike before

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


// --- Expand the Global Read Pointers for Multiple Voices ---
// Voice A (Major 3rd)
float read_ptrA1 = 0.0f;
float read_ptrA2 = BUFFER_SIZE / 2;

// Voice B (Perfect 5th)
float read_ptrB1 = 0.0f;
float read_ptrB2 = BUFFER_SIZE / 2;

void processAudio()
{
  // Define your chord intervals
  const float ratio_third = 1.2f; // minor 3rd
  const float ratio_fifth = 1.5f; // Perfect 5th

  // Volume scale factors so the combined chord doesn't clip
  const float dry_gain = 0.8f;   // 40% original voice
  const float third_gain = 0.1f; // 30% major third
  const float fifth_gain = 0.1f; // 30% perfect fifth

  for (int i = 0; i < sample_count; i++)
  {
    // 1. Get raw input sample
    int32_t right_sample = (int32_t)(sample_buffer[i] & 0xFFFFFFFF);
    float dry_signal = (float)right_sample;

    // 2. Push raw mic input to the delay buffer
    delay_buffer[write_ptr] = dry_signal;

    // ==========================================
    // VOICE A: MAJOR THIRD
    // ==========================================
    float distA1 = write_ptr - read_ptrA1;
    if (distA1 < 0)
      distA1 += BUFFER_SIZE;
    float fadeA1 = distA1 / (float)BUFFER_SIZE;
    float fadeA2 = 1.0f - fadeA1;

    int rpA1_f = (int)read_ptrA1;
    int rpA2_f = (int)read_ptrA2;
    int rpA1_c = (rpA1_f + 1) % BUFFER_SIZE;
    int rpA2_c = (rpA2_f + 1) % BUFFER_SIZE;

    float valA1 = delay_buffer[rpA1_f] + (read_ptrA1 - rpA1_f) * (delay_buffer[rpA1_c] - delay_buffer[rpA1_f]);
    float valA2 = delay_buffer[rpA2_f] + (read_ptrA2 - rpA2_f) * (delay_buffer[rpA2_c] - delay_buffer[rpA2_f]);
    float shifted_third = (valA1 * fadeA1) + (valA2 * fadeA2);

    // ==========================================
    // VOICE B: PERFECT FIFTH
    // ==========================================
    float distB1 = write_ptr - read_ptrB1;
    if (distB1 < 0)
      distB1 += BUFFER_SIZE;
    float fadeB1 = distB1 / (float)BUFFER_SIZE;
    float fadeB2 = 1.0f - fadeB1;

    int rpB1_f = (int)read_ptrB1;
    int rpB2_f = (int)read_ptrB2;
    int rpB1_c = (rpB1_f + 1) % BUFFER_SIZE;
    int rpB2_c = (rpB2_f + 1) % BUFFER_SIZE;

    float valB1 = delay_buffer[rpB1_f] + (read_ptrB1 - rpB1_f) * (delay_buffer[rpB1_c] - delay_buffer[rpB1_f]);
    float valB2 = delay_buffer[rpB2_f] + (read_ptrB2 - rpB2_f) * (delay_buffer[rpB2_c] - delay_buffer[rpB2_f]);
    float shifted_fifth = (valB1 * fadeB1) + (valB2 * fadeB2);

    // ==========================================
    // MIXING STAGE (Add them together safely)
    // ==========================================
    float mixed_signal = (dry_signal * dry_gain) +
                         (shifted_third * third_gain) +
                         (shifted_fifth * fifth_gain);

    // 7. Clamp the combined signal
    if (mixed_signal > INT32_MAX)
      mixed_signal = INT32_MAX;
    if (mixed_signal < INT32_MIN)
      mixed_signal = INT32_MIN;
    int32_t out_sample = (int32_t)mixed_signal;

    // 8. Output to stereo frame
    sample_buffer[i] = ((uint64_t)(uint32_t)out_sample << 32) | (uint32_t)out_sample;

    // 9. Increment all pointer groups
    write_ptr = (write_ptr + 1) % BUFFER_SIZE;

    read_ptrA1 += ratio_third;
    if (read_ptrA1 >= BUFFER_SIZE)
      read_ptrA1 -= BUFFER_SIZE;
    read_ptrA2 += ratio_third;
    if (read_ptrA2 >= BUFFER_SIZE)
      read_ptrA2 -= BUFFER_SIZE;

    read_ptrB1 += ratio_fifth;
    if (read_ptrB1 >= BUFFER_SIZE)
      read_ptrB1 -= BUFFER_SIZE;
    read_ptrB2 += ratio_fifth;
    if (read_ptrB2 >= BUFFER_SIZE)
      read_ptrB2 -= BUFFER_SIZE;
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

  // // Return pressed button number -> 0 default
  // button_pressed = getButton();

  // // Choose ratio based on button
  // pitch_ratio = updateInterval(button_pressed);

  // Process and shift the audio (Time-Domain)
  processAudio();

  // Write shifted audio to DAC/Speaker
  i2s_write(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesWritten, portMAX_DELAY);
}