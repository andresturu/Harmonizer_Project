


#include <Arduino.h>
#include "driver/i2s.h"
#include "Yin.h"
#include <math.h>
#include <string>

// Pure passthrough: read from INMP441 mic, write straight to speaker.
// No FFT, no processing at all — isolates whether the static is a hardware
// wiring/config issue or something introduced in software.

static const i2s_port_t i2s_num = I2S_NUM_0;

const uint16_t sample_count = 512;
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 1;
const uint16_t i2s_bytes = sample_count * i2s_bytes_per_sample * num_channels;

uint32_t sample_buffer[sample_count];

Yin yin;


const std::string noteNames[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
int find_midi_note(float pitch_freq);

static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = i2s_bits_per_sample,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = 0,
    .tx_desc_auto_clear = true,
    .fixed_mclk = -1
};

static const i2s_pin_config_t pin_config = {
    .bck_io_num = 19,
    .ws_io_num = 21,
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

  Yin_init(&yin, sample_count, YIN_DEFAULT_THRESHOLD);
}


void loop() {
  size_t bytesRead, bytesWritten;

  // 1. Read the raw I2S buffer
  i2s_read(i2s_num, sample_buffer, i2s_bytes, &bytesRead, portMAX_DELAY);

  float pitch_freq = Yin_getPitch(&yin,(int32_t*) sample_buffer);
  std::string note = find_midi_note(pitch_freq);

  //find volume level of input

  //use keypad to process button pressed(if any)

  //add right harmony notes based on button inputs at an adjusted volume


  Serial.printf("Note: %s, Fundamental Frequency: %.2f Hz\n", note.c_str(), pitch_freq);

  // 3. Send the clean audio to the PCM5102 DAC
  i2s_write(i2s_num, sample_buffer, bytesRead, &bytesWritten, portMAX_DELAY);
}


//A4 is 440 Hz, A4 is midi note 69
int find_midi_note(float pitch_freq) {
  int midi_note = round(12.0f* log2(pitch_freq/440.0f)+ 69.0f);
  int octave = midi_note/12 -1;
  int remainder = midi_note%12;

  //alternatively just return midi_note? honestly easier probably

  if (pitch_freq < 0) {
    return "no note";
  }
  return midi_note;
  //return noteNames[remainder] + std::to_string(octave);


}