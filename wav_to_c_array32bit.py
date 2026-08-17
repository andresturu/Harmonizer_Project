#!/usr/bin/env python3
"""
Batch-converts WAV note recordings into a single C header file
containing raw PCM sample arrays + a lookup table, for use on ESP32.

Usage (second argument is where to find .wav files and third argument where to put output_samples.h):
    python3 wav_to_c_array.py notes_folder/ output_samples.h

Expects WAV files named like: C4.wav, Db4.wav, D4.wav, ... (one per note)
Outputs 32-bit mono PCM at TARGET_SAMPLE_RATE, embedded as int32_t arrays.
"""

import sys
import os
import subprocess
import re

TARGET_SAMPLE_RATE = 16000  # change to 22050 if you want higher quality


def sanitize_name(filename):
    """Turn 'Db4.wav' into 'Db4' -> valid C identifier 'note_Db4'."""
    name = os.path.splitext(filename)[0]
    name = re.sub(r'[^A-Za-z0-9_]', '_', name)
    return f"note_{name}"

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
    # ffmpeg -i C4.wav -ar 16000 -ac 1 -sample_fmt s32 -f s16le C4.raw
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
        print("Usage: python3 wav_to_c_array.py <notes_folder> <output.h>")
        sys.exit(1)

    #sys.argv is a list that contains [wav_to_c_array.py, <notes_folder>, <output.h>]
    #so below code is just indexing for the specific paths as specified in terminal
    notes_folder = sys.argv[1]
    output_header = sys.argv[2]

    #puts every .wav in notes_folder into wav_files
    wav_files = sorted(f for f in os.listdir(notes_folder) if f.lower().endswith(".wav"))
    #if no wav_files in notes_folder
    if not wav_files:
        print(f"No .wav files found in {notes_folder}")
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
        #joins "notes/"" and "C4.wav" to make a full path -> "notes/C4.wav"
        wav_path = os.path.join(notes_folder, wav_file)
        # joins "notes/" and "C4.wav" to make -> "notes/C4.raw"
        raw_path = os.path.join(notes_folder, wav_file.replace(".wav", ".raw"))
        # from "C4.wav" sets var_name = note_C4
        var_name = sanitize_name(wav_file)

        print(f"Converting {wav_file} ...")
        convert_wav_to_raw(wav_path, raw_path)
        array_code, n_samples = raw_to_c_array(raw_path, var_name)
        header_lines.append(array_code)
        header_lines.append("")

        note_label = os.path.splitext(wav_file)[0]
        lookup_entries.append(f'    {{ "{note_label}", {var_name}, {var_name}_len }}')
        total_bytes += n_samples * 4

        os.remove(raw_path)  # cleanup intermediate file

    # struct + lookup table for easy access by note name in firmware
    header_lines.append("typedef struct {")
    header_lines.append("    const char* name;")
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