#ifndef VOICE
#define VOICE

#include <Arduino.h>

const int MAX_NOTES_PER_VOICE = 4;

/*
Explanation of relative_midi_notes

relative_midi_notes = [4, 7, 10, 13] would play a major9 chord above the reference midi note
relative_midi_notes = [-3, 0, 0, 0] would play a minor third below reference midi note, other three slots ignored

*/

struct Voice {
  int relative_midi_notes[MAX_NOTES_PER_VOICE]; //for getting the notes relative to the chord/interval desired, constant appropriate, it wont' change for a given instance of a voice
  bool active = false;
  int frozen_midi_notes[MAX_NOTES_PER_VOICE] = {0,0,0,0}; // after voice is activated, midi notes gets frozen and won't change, until voice is rest and activated again
  int curr_index = 0; //for looping voice repeatedly
};

void reset_voices(Voice* voices, int num_voices) {
    for (int i = 0; i<num_voices; i++) {
        voices[i].active = false;
        voices[i].curr_index = 0;
        for (int j = 0; j < MAX_NOTES_PER_VOICE; j++) {
            voices[i].frozen_midi_notes[j] = 0;
        }
    }
}

void activate_voice(Voice* voices, int voice_index, int curr_midi_note) {
    voices[voice_index].active = true;
    for (int i = 0; i < MAX_NOTES_PER_VOICE; i++) {
        if (voices[voice_index].relative_midi_notes[i] !=0) {
        voices[voice_index].frozen_midi_notes[i] = voices[voice_index].relative_midi_notes[i] + curr_midi_note;
        }
        else {
            voices[voice_index].frozen_midi_notes[i] = 0;
        }
    }
}


void print_voices(Voice* voices, int num_voices) {
    for (int i = 0; i < num_voices; i++) {
        Serial.printf("voice %d: active=%d curr_index=%d relative=[%d,%d,%d,%d] frozen=[%d,%d,%d,%d]\n",
            i,
            voices[i].active,
            voices[i].curr_index,
            voices[i].relative_midi_notes[0], voices[i].relative_midi_notes[1],
            voices[i].relative_midi_notes[2], voices[i].relative_midi_notes[3],
            voices[i].frozen_midi_notes[0], voices[i].frozen_midi_notes[1],
            voices[i].frozen_midi_notes[2], voices[i].frozen_midi_notes[3]);
    }
}

#endif