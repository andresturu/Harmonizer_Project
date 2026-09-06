#include <Arduino.h>
#include <esp_partition.h>
#include "driver/i2s.h"
#include "new_audio.h"
#include "voice.h"

const esp_partition_t *audio_partition = nullptr;

static const i2s_port_t i2s_num = I2S_NUM_0;

const uint16_t sample_count = 512;
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 1;
const uint16_t i2s_bytes = sample_count * i2s_bytes_per_sample * num_channels;

int32_t sample_buffer[sample_count];
int32_t voice_temp[sample_count];

// Which note to loop, hardcoded for this basic test
const int TEST_MIDI_NOTE = 44;

const int num_voices = 10;
//pass voices array to processAudio(), 
//then processAudio() will use the frozen_midi_notes numbers to play harmony output
Voice voices[num_voices] {
  {{ 0,  4,  7,  0}},   // major triad
  {{ 0,  3,  7,  0}},   // minor triad
  {{ 4,  7,  11, 14}},   // major9
  {{ 3,  7,  10, 14}},   // minor9
  {{ 3,  6,  9, 14}},   // diminished9
  {{-12, 0,  0,  0}},   // 
  {{ 12, 0,  0,  0}},   // octave up
  {{ -5, 0,  0,  0}},   // fourth below
  {{  7, 0,  0,  0}},   // fifth above
  {{  4, 7, 10, 13}},   // major9
};

static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
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
    .data_out_num = 22,
    .data_in_num = I2S_PIN_NO_CHANGE
};

void fillBlock(const NoteSample* note, uint32_t* cursor, int32_t* out_buffer);
void processAudio(Voice* voices, int num_voices);
const NoteSample* findNote(int midi_index) {
  for (int i = 0; i < note_table_count; i++) {
    if (note_table[i].midi_note == midi_index) {
      return &note_table[i];
    }
  }
  return nullptr;
}


void setup() {
  Serial.begin(115200);

  audio_partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA,
      (esp_partition_subtype_t)0x40,
      "audio"
  );
  if (audio_partition == nullptr) {
    Serial.println("ERROR: audio partition not found!");
    while (true) delay(1000);
  }
  Serial.printf("Audio partition OK: address=0x%lx size=%lu\n",
                audio_partition->address, audio_partition->size);

  esp_err_t err = i2s_driver_install(i2s_num, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install failed: %s (%d)\n", esp_err_to_name(err), err);
  }
  i2s_set_pin(i2s_num, &pin_config);

  const NoteSample* note = findNote(TEST_MIDI_NOTE);
  if (note == nullptr) {
    Serial.printf("Note %d not found in table!\n", TEST_MIDI_NOTE);
  } else {
    Serial.printf("Found note %d: offset=%lu length=%lu\n",
                  TEST_MIDI_NOTE, note->offset, note->length);
  }
}

void loop() {
  size_t bytesWritten;

  int curr_midi_note = TEST_MIDI_NOTE;
  static bool hi = true;
  if (hi) {
    activate_voice(voices, 2, curr_midi_note);
    hi = false;
  }
 
  processAudio(voices, num_voices);

  i2s_write(i2s_num, sample_buffer, i2s_bytes, &bytesWritten, portMAX_DELAY);
}


void processAudio(Voice* voices, int num_voices) {
  // clear the mix buffer before summing voices into it
  for (int i = 0; i < sample_count; i++) {
    sample_buffer[i] = 0;
  }

  for (int v = 0; v < num_voices; v++) {
    if (!voices[v].active) continue;

    for (int n = 0; n < MAX_NOTES_PER_VOICE; n++) {
      int midi_note = voices[v].frozen_midi_notes[n];
      if (midi_note == 0) continue;   // unused slot

      const NoteSample* note = findNote(midi_note);
      if (note == nullptr) continue;

      // this note's own cursor lives at voices[v].curr_index[n]
      // fill voice_temp buffer with note info, based on its current index
      fillBlock(note, &voices[v].curr_index[n], voice_temp);

      // add note info to sample_buffer
      for (int i = 0; i < sample_count; i++) {
        sample_buffer[i] += voice_temp[i];
      }
    }
  }
}

void fillBlock(const NoteSample* note, uint32_t* cursor, int32_t* out_buffer) {
  uint32_t remaining = note->loop_end - *cursor;

  if (remaining >= sample_count) {
    // Whole block fits before this note's loop point
    esp_partition_read(
        audio_partition,
        note->offset + (*cursor) * sizeof(int32_t),
        out_buffer,
        sample_count * sizeof(int32_t)
    );
    *cursor += sample_count;

  } else {
    // Block spans the loop point: read the tail, then the head, in two calls
    esp_partition_read(
        audio_partition,
        note->offset + (*cursor) * sizeof(int32_t),
        out_buffer,
        remaining * sizeof(int32_t)
    );

    uint32_t head_needed = sample_count - remaining;
    esp_partition_read(
        audio_partition,
        note->offset + note->loop_start * sizeof(int32_t),
        out_buffer + remaining,
        head_needed * sizeof(int32_t)
    );

    *cursor = note->loop_start + head_needed;
  }

  if (*cursor >= note->loop_end) {
    *cursor = note->loop_start;
  }
}