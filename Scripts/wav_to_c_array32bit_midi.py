#!/usr/bin/env python3
"""
Batch-converts WAV note recordings into:
  - a raw PCM binary blob (all notes concatenated back-to-back), meant to be
    flashed directly onto a dedicated ESP32 data partition (see
    partitions.csv, partition named "audio"), and
  - a small C header with a lookup table describing each note's byte offset,
    sample count, and loop points within that blob.

Samples are NOT compiled in as const arrays anymore: the original ESP32's
flash-mapped rodata region (drom0_0_seg, in the Arduino framework's
memory.ld) is hard-capped at 4MB regardless of the physical flash chip size
or partitions.csv, so multiple full-length note recordings don't fit that
way. Instead, note data is read at runtime via esp_partition_read(), which
goes through the SPI flash driver rather than that fixed-size memory-mapped
window.

Usage:
    python3 wav_to_c_array32bit_midi.py notes_folder/ output_samples.h output_audio.bin

Expects WAV files named by their MIDI note number, e.g.: 60.wav, 64.wav, 67.wav, ...
Outputs 32-bit mono PCM at TARGET_SAMPLE_RATE.
"""

import sys
import os
import subprocess

TARGET_SAMPLE_RATE = 16000  # change to 22050 if you want higher quality
BYTES_PER_SAMPLE = 4        # s32

# Placeholder loop points: a fixed fraction of each note's own length.
# A dedicated loop-point-finder pass (run between this script and the final
# header) is expected to overwrite loop_start/loop_end with values chosen
# from the actual waveform (matching phase/amplitude at the splice).
LOOP_START_FRACTION = 0.20


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

def compute_loop_points(n_samples):
    """Placeholder loop_start/loop_end for a note of this length. See the
    LOOP_START_FRACTION comment above: these are meant to be refined later."""
    loop_start = int(LOOP_START_FRACTION * n_samples)
    loop_end = n_samples
    return loop_start, loop_end

def main():
    if len(sys.argv) != 4:
        print("Usage: python3 wav_to_c_array32bit_midi.py <notes_folder> <output.h> <output_audio.bin>")
        sys.exit(1)

    notes_folder = sys.argv[1]
    output_header = sys.argv[2]
    output_bin = sys.argv[3]

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

    lookup_entries = []
    byte_offset = 0

    with open(output_bin, "wb") as bin_out:
        for wav_file in wav_files:
            midi_note = parse_midi_note(wav_file)
            #joins "notes/"" and "60.wav" to make a full path -> "notes/60.wav"
            wav_path = os.path.join(notes_folder, wav_file)
            # joins "notes/" and "60.wav" to make -> "notes/60.raw"
            raw_path = os.path.join(notes_folder, wav_file.replace(".wav", ".raw"))

            print(f"Converting {wav_file} (midi note {midi_note}) ...")
            convert_wav_to_raw(wav_path, raw_path)

            with open(raw_path, "rb") as f:
                data = f.read()
            n_samples = len(data) // BYTES_PER_SAMPLE
            bin_out.write(data)

            loop_start, loop_end = compute_loop_points(n_samples)
            lookup_entries.append(
                f'    {{ {midi_note}, {byte_offset}, {n_samples}, {loop_start}, {loop_end} }}'
            )

            byte_offset += n_samples * BYTES_PER_SAMPLE
            os.remove(raw_path)  # cleanup intermediate file

    header_lines = []
    header_lines.append("// Auto-generated note metadata. Do not edit by hand.")
    header_lines.append(f"// Sample rate: {TARGET_SAMPLE_RATE} Hz, 32-bit mono PCM")
    header_lines.append("// Actual sample data is NOT in this file - it's in the .bin blob")
    header_lines.append("// written alongside it, meant to be flashed onto the \"audio\"")
    header_lines.append("// partition (see partitions.csv) and read at runtime via")
    header_lines.append("// esp_partition_read().")
    header_lines.append("#pragma once")
    header_lines.append("#include <stdint.h>")
    header_lines.append("")
    header_lines.append("typedef struct {")
    header_lines.append("    int midi_note;")
    header_lines.append("    uint32_t offset;      // byte offset into the audio partition")
    header_lines.append("    uint32_t length;      // sample count")
    header_lines.append("    uint32_t loop_start;  // sample index, relative to this note's own start")
    header_lines.append("    uint32_t loop_end;    // sample index, relative to this note's own start")
    header_lines.append("} NoteSample;")
    header_lines.append("")
    header_lines.append(f"const NoteSample note_table[{len(lookup_entries)}] = {{")
    header_lines.append(",\n".join(lookup_entries))
    header_lines.append("};")
    header_lines.append(f"const int note_table_count = {len(lookup_entries)};")

    with open(output_header, "w") as f:
        f.write("\n".join(header_lines))

    print(f"\nDone. Wrote {output_header} and {output_bin}")
    print(f"Total audio data: {byte_offset / 1024:.1f} KB across {len(wav_files)} notes")

if __name__ == "__main__":
    main()
