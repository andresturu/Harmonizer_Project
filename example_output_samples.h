// Auto-generated note sample data. Do not edit by hand.
// Sample rate: 16000 Hz, 32-bit mono PCM
#pragma once
#include <stdint.h>

const int32_t note_C4[] = {
    0, -125000, 305000, 1042000, 890000, -45000, -1203000, -980000, 120000, 950000, 1100000, 200000,
    -800000, -1150000, -300000, 600000, 1200000, 500000, -400000, -1000000, -600000, 200000, 800000, 600000,
    // ... remaining PCM sample values ...
};
const uint32_t note_C4_len = 16000;

const int32_t note_Db4[] = {
    0, -80000, 210000, 950000, 800000, -20000, -1100000, -850000, 90000, 880000, 1020000, 150000,
    // ... remaining PCM sample values ...
};
const uint32_t note_Db4_len = 16000;

typedef struct {
    const char* name;
    const int32_t* samples;
    uint32_t length;
} NoteSample;

const NoteSample note_table[2] = {
    { "C4", note_C4, note_C4_len },
    { "Db4", note_Db4, note_Db4_len }
};
const int note_table_count = 2;