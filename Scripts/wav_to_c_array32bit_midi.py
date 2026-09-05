#!/usr/bin/env python3
"""
Batch-converts WAV note recordings into a single C header file
containing raw PCM sample arrays + a lookup table, for use on ESP32.

Usage (second argument is where to find .wav files and third argument where to put output_samples.h):
    python3 wav_to_c_array32bit_midi.py notes_folder/ output_samples.h

Expects WAV files named by their MIDI note number, e.g.: 60.wav, 64.wav, 67.wav, ...
Outputs 32-bit mono PCM at TARGET_SAMPLE_RATE, embedded as int32_t arrays.
"""

import sys
import os
import subprocess

TARGET_SAMPLE_RATE = 16000  # change to 22050 if you want higher quality


def parse_midi_note(filename):
    """Turn '60.wav' into the int 60. Errors if the filename isn't a plain integer."""
    stem = os.path.splitext(filename)[0]
    try:
        return int(stem)
    except ValueError:
        raise ValueError(
            f"'{filename}' isn't named as a MIDI note number (expected e.g. '60.wav'), got stem '{stem}'"
        )

def var_name_for(midi_note):
    """midi note 60 -> valid C identifier 'note_60'."""
    return f"note_{midi_note}"

def convert_wav_to_raw(wav_path, raw_path):
    cmd = [
        "ffmpeg", "-y", "-i", wav_path,
        "-ar", str(TARGET_SAMPLE_RATE),
        "-ac", "1",
        "-sample_fmt", "s32",
        "-f", "s32le",
        raw_path
    ]
    #basically makes Python type into the terminal
    # ffmpeg -i 60.wav -ar 16000 -ac 1 -sample_fmt s32 -f s16le 60.raw
    # converts from .wav into raw desired stuff
    subprocess.run(cmd, check=True, capture_output=True)

def raw_to_c_array(raw_path, var_name):
    with open(raw_path, "rb") as f:
        data = f.read()
    # interpret as little-endian int32 samples
    n_samples = len(data) // 4
    samples = []
    for i in range(n_samples):
        val = int.from_bytes(data[i*4:i*4+4], byteorder="little", signed=True)
        samples.append(str(val))

    lines = []
    lines.append(f"const int32_t {var_name}[] = {{")
    # wrap at ~12 values per line for readability
    for i in range(0, len(samples), 12):
        lines.append("    " + ", ".join(samples[i:i+12]) + ",")
    lines.append("};")
    lines.append(f"const uint32_t {var_name}_len = {n_samples};")
    return "\n".join(lines), n_samples

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 wav_to_c_array32bit_midi.py <notes_folder> <output.h>")
        sys.exit(1)

    #sys.argv is a list that contains [wav_to_c_array32bit_midi.py, <notes_folder>, <output.h>]
    #so below code is just indexing for the specific paths as specified in terminal
    notes_folder = sys.argv[1]
    output_header = sys.argv[2]

    #puts every .wav in notes_folder into wav_files
    wav_files = [f for f in os.listdir(notes_folder) if f.lower().endswith(".wav")]
    #if no wav_files in notes_folder
    if not wav_files:
        print(f"No .wav files found in {notes_folder}")
        sys.exit(1)

    # sort numerically by midi note, not alphabetically by filename
    # (this also validates every filename up front, before any ffmpeg work happens)
    try:
        wav_files = sorted(wav_files, key=parse_midi_note)
    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)

    header_lines = []
    header_lines.append("// Auto-generated note sample data. Do not edit by hand.")
    header_lines.append(f"// Sample rate: {TARGET_SAMPLE_RATE} Hz, 32-bit mono PCM")
    header_lines.append("#pragma once")
    header_lines.append("#include <stdint.h>")
    header_lines.append("")

    lookup_entries = []
    total_bytes = 0

    for wav_file in wav_files:
        midi_note = parse_midi_note(wav_file)
        #joins "notes/"" and "60.wav" to make a full path -> "notes/60.wav"
        wav_path = os.path.join(notes_folder, wav_file)
        # joins "notes/" and "60.wav" to make -> "notes/60.raw"
        raw_path = os.path.join(notes_folder, wav_file.replace(".wav", ".raw"))
        # midi_note 60 -> var_name = note_60
        var_name = var_name_for(midi_note)

        print(f"Converting {wav_file} (midi note {midi_note}) ...")
        convert_wav_to_raw(wav_path, raw_path)
        array_code, n_samples = raw_to_c_array(raw_path, var_name)
        header_lines.append(array_code)
        header_lines.append("")

        lookup_entries.append(f'    {{ {midi_note}, {var_name}, {var_name}_len }}')
        total_bytes += n_samples * 4

        os.remove(raw_path)  # cleanup intermediate file

    # struct + lookup table for easy access by midi note in firmware
    header_lines.append("typedef struct {")
    header_lines.append("    int midi_note;")
    header_lines.append("    const int32_t* samples;")
    header_lines.append("    uint32_t length;")
    header_lines.append("} NoteSample;")
    header_lines.append("")
    header_lines.append(f"const NoteSample note_table[{len(lookup_entries)}] = {{")
    header_lines.append(",\n".join(lookup_entries))
    header_lines.append("};")
    header_lines.append(f"const int note_table_count = {len(lookup_entries)};")

    with open(output_header, "w") as f:
        f.write("\n".join(header_lines))

    print(f"\nDone. Wrote {output_header}")
    print(f"Total sample data: {total_bytes / 1024:.1f} KB across {len(wav_files)} notes")

if __name__ == "__main__":
    main()
