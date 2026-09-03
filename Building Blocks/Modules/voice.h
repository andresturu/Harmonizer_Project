#ifndef VOICE
#define VOICE

const int MAX_NOTES_PER_VOICE = 4;

/*
Explanation of relative_midi_notes

relative_midi_notes = [4, 7, 10, 13] would play a major9 chord above the reference midi note
relative_midi_notes = [-3, 0, 0, 0] would play a minor third below reference midi note, other three slots ignored

*/

struct Voice {
  bool active = false;
  const int relative_midi_notes[MAX_NOTES_PER_VOICE]; //for getting the notes relative to the chord/interval desired, constant appropriate, it wont' change for a given instance of a voice
  int frozen_midi_notes[MAX_NOTES_PER_VOICE] = {0,0,0,0}; // after voice is activated, midi notes gets frozen and won't change, until voice is rest and activated again
  int curr_index = 0; //for looping voice repeatedly
};

void reset_voices(Voice* voices, int num_voices) {
    for (int i = 0; i<num_voices; i++) {
        voices[i].active = false;
        voices[i].curr_index = 0;
        voices[i].frozen_midi_notes = {0,0,0,0}; // wrong syntax somehow?
    }
}

void activate_voice(Voice* voices, int voice_index, int curr_mini_note) {
    voices[voice_index].active = true;
    for (int i = 0; i < MAX_NOTES_PER_VOICE; i++) {
        voices[voice_index].frozen_midi_notes[i] += curr_mini_note;
    }
}


#endif