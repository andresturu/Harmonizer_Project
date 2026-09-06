// Auto-generated note metadata. Do not edit by hand.
// Sample rate: 16000 Hz, 32-bit mono PCM
// Actual sample data is NOT in this file - it's in the .bin blob
// written alongside it, meant to be flashed onto the "audio"
// partition (see partitions.csv) and read at runtime via
// esp_partition_read().
#pragma once
#include <stdint.h>

typedef struct {
    int midi_note;
    uint32_t offset;      // byte offset into the audio partition
    uint32_t length;      // sample count
    uint32_t loop_start;  // sample index, relative to this note's own start
    uint32_t loop_end;    // sample index, relative to this note's own start
} NoteSample;

const NoteSample note_table[29] = {
    { 44, 0, 62000, 12400, 62000 },
    { 45, 248000, 68500, 13700, 68500 },
    { 46, 522000, 61000, 12200, 61000 },
    { 47, 766000, 53500, 10700, 53500 },
    { 48, 980000, 71000, 14200, 71000 },
    { 49, 1264000, 64500, 12900, 64500 },
    { 50, 1522000, 58500, 11700, 58500 },
    { 51, 1756000, 61500, 12300, 61500 },
    { 52, 2002000, 57500, 11500, 57500 },
    { 53, 2232000, 67500, 13500, 67500 },
    { 54, 2502000, 68250, 13650, 68250 },
    { 55, 2775000, 68625, 13725, 68625 },
    { 56, 3049500, 63500, 12700, 63500 },
    { 57, 3303500, 66750, 13350, 66750 },
    { 58, 3570500, 70125, 14025, 70125 },
    { 59, 3851000, 69250, 13850, 69250 },
    { 60, 4128000, 55500, 11100, 55500 },
    { 61, 4350000, 59750, 11950, 59750 },
    { 62, 4589000, 68625, 13725, 68625 },
    { 63, 4863500, 73750, 14750, 73750 },
    { 64, 5158500, 57000, 11400, 57000 },
    { 65, 5386500, 75250, 15050, 75250 },
    { 66, 5687500, 70750, 14150, 70750 },
    { 67, 5970500, 70125, 14025, 70125 },
    { 68, 6251000, 75625, 15125, 75625 },
    { 69, 6553500, 57750, 11550, 57750 },
    { 70, 6784500, 65125, 13025, 65125 },
    { 71, 7045000, 63000, 12600, 63000 },
    { 72, 7297000, 55875, 11175, 55875 }
};
const int note_table_count = 29;