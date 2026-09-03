//implementation notes!!
// instead of having each Voice follow the note being played,
// after voice is activated, its midi notes are FROZEN, so that voice maintains the original note
// would allow for more musicality
// Example: Plays C on instrument, selects major third so Voice is frozen on midi E note, 
// then user goes down to B, but voice is still playing the same midi E note

#include <Arduino.h>
#include "driver/i2s.h"
#include "shortclip3.h"
#include "volumescaling.h"
#include "pitchreader.h"
#include "voice.h"

static const i2s_port_t i2s_num = I2S_NUM_0;
const uint16_t sample_count = 512;
const i2s_bits_per_sample_t i2s_bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
const uint8_t i2s_bytes_per_sample = i2s_bits_per_sample / 8;
const int num_channels = 1; 
const uint16_t i2s_bytes = sample_count * i2s_bytes_per_sample * num_channels;
int32_t sample_buffer[sample_count];

int num_voices = 10;
//pass voices array to processAudio(), 
//then processAudio() will use the frozen_midi_notes numbers to play harmony output
Voice voices[num_voices];


Yin yin;

// Loop parameters
const int LOOP_START = 12030;   // End of attack phase (~0.5s at 16kHz)
const int LOOP_END   = 24098;  // End of sustain phase (~1.5s at 16kHz)
const int FADE_LEN   = 206;    // Crossfade duration in samples

void processAudio(int& current_index, int start_loop, int end_loop);

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

  // Pace execution using I2S timing
  i2s_read(i2s_num, sample_buffer, i2s_bytes, &bytesRead, portMAX_DELAY);

  // calculate harmony_gain for harmonizer output in realistic logarithmic/dB scale
  float harmony_gain = get_scaled_amplitude(sample_buffer, sample_count);
  
  // retrieve pitch and corresponding midi note number from audio input
  float pitch_freq = Yin_getPitch(&yin,(int32_t*) sample_buffer);
  int curr_midi_note = find_midi_note(pitch_freq);
  Serial.printf("midi_Note: %d, Fundamental Frequency: %.2f Hz\n", midi_note, pitch_freq);

  // check which buttons were pressed on keypad, and activate/deactivate buttons based on it
  KeyEvent keyevent = checkKeypad();
  if (keyevent.pressed)
  {
    //could use switch/case instead of if's
    // if reset button is pressed, stop all harmonizer voices
    if (keyevent.keyID == 1)
    {
      reset_voices(voices, num_voices); // setting active to false, unfreezing, setting curr_index to 0
    }
    //flip voicings up/down for next press, NOT past voicings!
    else if (keyevent.keyID == 2) {
      //IMPLEMENT
    }
    // turn on a voicing, freeze the harmonized mini notes
    else
    {
      int desired_voice_index = keyevent.keyID-3; //might require changing in the future
      activate_voice(voices, desired_voice_index, curr_midi_note);
    }
  }

  // Process the audio block with crossfading, implement counter++ for EACH voice independently!!
  // because each voice starts independently
  processAudio(current_sample_index, LOOP_START, LOOP_END);

  // scale harmonizer output based on calculated gain, wondering if sample_buffer is getting used way too much
  for (int i = 0; i < sample_count; i++) {
    sample_buffer[i] *= harmony_gain;
  }

  // Write out to DAC
  i2s_write(i2s_num, sample_buffer, i2s_bytes, &bytesWritten, portMAX_DELAY);
}

void processAudio(Voice* voices) {
  //zero out sample buffer (should I use a different buffer for speed? and just pass sample_buffer by reference at the end?
  for (int i = 0; i < sample_count; i++) {
    sample_buffer[i] = 0;
  }

  //definitely limit amount of voices that can be active at once, perhaps to limit computation effort or just make cleaner
  
  for (int i = 0; i < num_voices; i++) {
    if (voices[i].active) {
      //add the appropriate midi note based on voices[i].frozen_midi_notes[]
      //add crossfading based on voices[i].index


      voices[i].curr_index ++;

      if (current_index >= end_loop) {
      current_index = start_loop + FADE_LEN;
      }
    }
  }

}


void processAudio(int& current_index, int start_loop, int end_loop) {
  const NoteSample* note = &note_table[0];

  for (int i = 0; i < sample_count; i++) {
    
    // Check if we are inside the crossfade boundary region
    if (current_index >= (end_loop - FADE_LEN)) {
      
      // Calculate progress through crossfade: 0.0f at start, 1.0f at end_loop
      int fade_pos = current_index - (end_loop - FADE_LEN);
      float progress = (float)fade_pos / (float)FADE_LEN;

      // Primary sample ending (fading out)
      int32_t sample_out = note->samples[current_index];
      
      // Secondary sample starting from loop head (fading in)
      int32_t sample_in = note->samples[start_loop + fade_pos];

      float fade_out_gain = cosf(progress * (float)M_PI_2);   // 1 -> 0
      float fade_in_gain   = sinf(progress * (float)M_PI_2);  // 0 -> 1

      sample_buffer[i] = (int32_t)(fade_out_gain * sample_out + fade_in_gain * sample_in);

      // Blend the two samples smoothly
      //sample_buffer[i] = (int32_t)((1.0f - progress) * sample_out + progress * sample_in);

    } else {
      // Normal playback outside crossfade region
      sample_buffer[i] = (uint32_t)note->samples[current_index];
    }

    current_index++;

    // When reaching end_loop, jump directly to start_loop + FADE_LEN
    if (current_index >= end_loop) {
      current_index = start_loop + FADE_LEN;
    }
  }
}