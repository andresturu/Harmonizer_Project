#include <Arduino.h>
#include "driver/i2s.h"
#include "better_sound_data.h"

static const i2s_port_t i2s_num = I2S_NUM_0;

const uint16_t sample_count = 512;
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 1;
const uint16_t i2s_bytes = sample_count * i2s_bytes_per_sample * num_channels;

uint32_t sample_buffer[sample_count];

// Loop parameters are normalized as fractions of each note's own sample length,
// rather than fixed absolute indices, since notes range from ~17000 to ~37000
// samples long. Fractions come from the original tuning on note 44 (length 20000):
// loop started at sample 12030 and the crossfade lasted 206 samples out of the
// 8068-sample sustain region (12030 -> 20098).
const float LOOP_START_FRACTION = 12030.0f / 20000.0f;
const float FADE_FRACTION = 206.0f / (20098.0f - 12030.0f);

struct LoopParams {
  int start;
  int end;
  int fade;
};

LoopParams computeLoopParams(uint32_t length);
const NoteSample* findNote(int midi_index);
void processAudio(int& current_index, const NoteSample* note, const LoopParams& loop);

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

}

void loop() {
  size_t bytesRead, bytesWritten;
  static int current_sample_index = 0;
  static unsigned long previous_time = millis();
  static int first_midi = 44;
  static int last_midi = 72;
  static int midi_index = first_midi;
  static int prev_midi_index = first_midi; // matches midi_index initially so the very first note still plays its full attack from sample 0

  // Pace execution using I2S timing
  i2s_read(i2s_num, sample_buffer, i2s_bytes, &bytesRead, portMAX_DELAY);

  unsigned long current_time = millis();
  if (current_time - previous_time > 3000) {
    previous_time = millis();
    (midi_index >= last_midi) ? midi_index = first_midi: midi_index ++;
  }

  const NoteSample* note = findNote(midi_index);
  if (note == nullptr) return;
  LoopParams loop_params = computeLoopParams(note->length);

  if (midi_index != prev_midi_index) {
    current_sample_index = loop_params.start; // restart cleanly for the new note; each note has its own loop bounds
    prev_midi_index = midi_index;
  }

  // Process the audio block with crossfading (updates current_sample_index)
  processAudio(current_sample_index, note, loop_params);

  // Write out to DAC
  i2s_write(i2s_num, sample_buffer, i2s_bytes, &bytesWritten, portMAX_DELAY);
}

LoopParams computeLoopParams(uint32_t length) {
  LoopParams p;
  p.end = (int)length;
  p.start = (int)(LOOP_START_FRACTION * length);
  int region = p.end - p.start;
  p.fade = (int)(FADE_FRACTION * region);
  if (p.fade < 1) p.fade = 1;
  return p;
}

const NoteSample* findNote(int midi_index) {
  for (int i = 0; i < note_table_count; i++) {
    if (note_table[i].midi_note == midi_index) {
      return &note_table[i];
    }
  }
  return nullptr;
}

void processAudio(int& current_index, const NoteSample* note, const LoopParams& loop) {
  for (int i = 0; i < sample_count; i++) {

    // Check if we are inside the crossfade boundary region
    if (current_index >= (loop.end - loop.fade)) {

      // Calculate progress through crossfade: 0.0f at start, 1.0f at loop.end
      int fade_pos = current_index - (loop.end - loop.fade);
      float progress = (float)fade_pos / (float)loop.fade;

      // Primary sample ending (fading out)
      int32_t sample_out = note->samples[current_index];

      // Secondary sample starting from loop head (fading in)
      int32_t sample_in = note->samples[loop.start + fade_pos];

      float fade_out_gain = cosf(progress * (float)M_PI_2);   // 1 -> 0
      float fade_in_gain   = sinf(progress * (float)M_PI_2);  // 0 -> 1

      sample_buffer[i] = (int32_t)(fade_out_gain * sample_out + fade_in_gain * sample_in);

    } else {
      // Normal playback outside crossfade region
      sample_buffer[i] = (uint32_t)note->samples[current_index];
    }

    current_index++;

    // When reaching loop.end, jump directly to loop.start + loop.fade
    if (current_index >= loop.end) {
      current_index = loop.start + loop.fade;
    }
  }
}
