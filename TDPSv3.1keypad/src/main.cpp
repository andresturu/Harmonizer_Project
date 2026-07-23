#include <Arduino.h>
#include <keypad.h>
#include <iostream>
#include <string>
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



const int num_harmony_buttons = 8;
bool harmony_notes[num_harmony_buttons];
float harmony_freqs[] = {0.80f, 0.83f, 1.2f, 1.25f, 1.33f, 1.5f, 1.60, 2.0f};
struct Voice {
  bool active = false;
  float ratio = 1.0f;
  float read_ptr1 = 0.0f;
  float read_ptr2 = BUFFER_SIZE / 2.0f;
};

Voice voices[num_harmony_buttons];
const int MAX_ACTIVE_VOICES = 4;

//[0,0,0] // 100% original toni
//[1, 0, 1] //40% original tonic, rest 60% is split up between other two

void process_audio();

void setup()
{
  Serial.begin(115200);
  setUpKeypad();

  for (int i = 0; i < num_harmony_buttons; i++)
  {
    harmony_notes[i] = 0;
  }
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

  // Read raw audio from mic
  i2s_read(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesRead, portMAX_DELAY);

  // returns KeyEvent, with attributes .pressed, .row, .col, and .keyID
  KeyEvent keyevent = checkKeypad();
  // if a key was pressed, modify harmony_notes[] according, should put this in keypad.h and keypad.cpp for organization
  if (keyevent.pressed)
  {
    // if reset button is pressed, reset to only playing the tonic
    if (keyevent.keyID == 1)
    {
      //std::cout << "reset vector" << std::endl;
      for (int i = 0; i < num_harmony_buttons; i++)
      {
        harmony_notes[i] = 0;
      }
    }
    // if button other than the reset button is pressed, add button to notes
    else
    {
      //std::cout << "add another harmony note\n";
      harmony_notes[keyevent.keyID -2 ] = 1;
    }
    
    printKeypad();
    for (int note : harmony_notes) {
      std::cout << note << " " ;
    }
    std::cout << "\n";
    
  }

  process_audio();


    // Write shifted audio to DAC/Speaker
  i2s_write(i2s_num, sample_buffer, i2s_read_size_bytes, &BytesWritten, portMAX_DELAY);

}


void process_audio() {
  // 1. Reset and assign active voices from pressed harmony buttons
  int active_count = 0;

  for (int i = 0; i < num_harmony_buttons; i++) {
    // Only activate if button has been pressed AND we haven't hit our 4-voice limit
    if (harmony_notes[i] && active_count < MAX_ACTIVE_VOICES) {
      voices[i].active = true;
      voices[i].ratio = harmony_freqs[i];
      active_count++;
    } else {
      voices[i].active = false;
    }
  }

  // 2. Dynamic Gain Balance (40% dry, 60% split across active voices)
  float dry_gain = (active_count > 0) ? 0.3f : 1.0f;
  float voice_gain = (active_count > 0) ? (0.7f / (float)active_count) : 0.0f;

  // 3. Audio Processing Loop
  for (int i = 0; i < sample_count; i++)
  {
    int32_t right_sample = (int32_t)(sample_buffer[i] & 0xFFFFFFFF);
    float dry_signal = (float)right_sample;

    delay_buffer[write_ptr] = dry_signal;
    float mixed_signal = dry_signal * dry_gain;

    // --- Process All Active Voices ---
    for (int v = 0; v < num_harmony_buttons; v++)
    {
      if (!voices[v].active) continue; //skip inactive voices
      Voice &v_ref = voices[v];

      // Calculate grain crossfade distances
      float dist1 = write_ptr - v_ref.read_ptr1;
      if (dist1 < 0) dist1 += BUFFER_SIZE;
      float fade1 = dist1 / (float)BUFFER_SIZE;
      float fade2 = 1.0f - fade1;

      int rp1_f = (int)v_ref.read_ptr1;
      int rp2_f = (int)v_ref.read_ptr2;
      int rp1_c = (rp1_f + 1) % BUFFER_SIZE;
      int rp2_c = (rp2_f + 1) % BUFFER_SIZE;

      // Interpolate buffer samples
      float val1 = delay_buffer[rp1_f] + (v_ref.read_ptr1 - rp1_f) * (delay_buffer[rp1_c] - delay_buffer[rp1_f]);
      float val2 = delay_buffer[rp2_f] + (v_ref.read_ptr2 - rp2_f) * (delay_buffer[rp2_c] - delay_buffer[rp2_f]);

      float shifted_sample = (val1 * fade1) + (val2 * fade2);
      mixed_signal += shifted_sample * voice_gain;
    }

    // Clamp output signal
    if (mixed_signal > INT32_MAX) mixed_signal = INT32_MAX;
    if (mixed_signal < INT32_MIN) mixed_signal = INT32_MIN;
    int32_t out_sample = (int32_t)mixed_signal;

    // Pack stereo sample frame
    sample_buffer[i] = ((uint64_t)(uint32_t)out_sample << 32) | (uint32_t)out_sample;

    // Advance write pointer
    write_ptr = (write_ptr + 1) % BUFFER_SIZE;

    // Advance read pointers for active voices
    for (int v = 0; v < num_harmony_buttons; v++)
    {
      if (voices[v].active) {
        voices[v].read_ptr1 += voices[v].ratio;
        if (voices[v].read_ptr1 >= BUFFER_SIZE) voices[v].read_ptr1 -= BUFFER_SIZE;

        voices[v].read_ptr2 += voices[v].ratio;
        if (voices[v].read_ptr2 >= BUFFER_SIZE) voices[v].read_ptr2 -= BUFFER_SIZE;
    
      }
    }
  }
}