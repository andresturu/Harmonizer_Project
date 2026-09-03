#!/usr/bin/env python3
"""
find_loop_points.py
 
Finds good LOOP_START / LOOP_END sample indices for seamless looping of a
sustained note (e.g. saxophone), for use in an ESP32 crossfade-looping synth.
 
- Converts the WAV to 32-bit mono PCM at TARGET_SAMPLE_RATE (matches your
  wav_to_c_array.py pipeline exactly, so indices line up with your header).
- Detects where the attack transient ends (so loop points are never chosen
  inside the attack).
- Estimates the fundamental period via autocorrelation in a stable region.
- Searches for rising zero crossings near your rough start/end guesses,
  constrained to the post-attack region.
- Scores candidate (start, end) pairs by how close their spacing is to a
  whole number of fundamental periods, and how close their local slopes
  match, then picks the best pair.
 
Usage:
    python3 find_loop_points.py path/to/note.wav [--start 12000] [--end 20000] [--window 200] [--rate 16000]
 
Requires: numpy, ffmpeg (on PATH)
"""
 
import sys
import os
import subprocess
import argparse
import numpy as np
 
 
def convert_wav_to_raw(wav_path, raw_path, sample_rate):
    cmd = [
        "ffmpeg", "-y", "-i", wav_path,
        "-ar", str(sample_rate),
        "-ac", "1",
        "-sample_fmt", "s32",
        "-f", "s32le",
        raw_path
    ]
    subprocess.run(cmd, check=True, capture_output=True)
 
 
def load_samples(wav_path, sample_rate):
    raw_path = wav_path + ".raw_tmp"
    convert_wav_to_raw(wav_path, raw_path, sample_rate)
    samples = np.fromfile(raw_path, dtype='<i4')  # little-endian int32
    os.remove(raw_path)
    return samples
 
 
def find_sustain_start(samples, frame_size=256, stability_thresh=0.05, lookback=10):
    """Find where the amplitude envelope flattens out (attack -> sustain)."""
    n_frames = (len(samples) - frame_size) // frame_size
    envelope = np.empty(n_frames)
    for i in range(n_frames):
        frame = samples[i * frame_size:(i + 1) * frame_size].astype(np.float64)
        envelope[i] = np.sqrt(np.mean(frame ** 2))
 
    peak = envelope.max()
    for i in range(lookback, len(envelope)):
        window = envelope[i - lookback:i]
        mean = window.mean()
        if mean <= 0:
            continue
        if (window.std() / mean) < stability_thresh and mean > 0.5 * peak:
            return i * frame_size
    return len(samples) // 4  # fallback guess if nothing found
 
 
def estimate_period(samples, center, sample_rate, window=4000,
                     min_hz=100, max_hz=1000):
    """Estimate fundamental period (in samples) via autocorrelation."""
    lo = max(0, center - window // 2)
    hi = min(len(samples), center + window // 2)
    seg = samples[lo:hi].astype(np.float64)
    seg -= seg.mean()
 
    corr = np.correlate(seg, seg, mode='full')[len(seg) - 1:]
 
    min_lag = int(sample_rate / max_hz)
    max_lag = int(sample_rate / min_hz)
    max_lag = min(max_lag, len(corr) - 1)
 
    if min_lag >= max_lag:
        raise ValueError("min_hz/max_hz range too narrow for this window size")
 
    search = corr[min_lag:max_lag]
    peak_lag = min_lag + int(np.argmax(search))
    return peak_lag
 
 
def find_rising_zero_crossings(samples, center, window, floor):
    """Rising zero crossings within [center-window, center+window], excluding
    anything before `floor` (the detected sustain start)."""
    lo = max(floor, center - window)
    hi = min(len(samples) - 1, center + window)
    crossings = []
    for i in range(lo, hi):
        if samples[i] <= 0 and samples[i + 1] > 0:
            crossings.append(i)
    return crossings
 
 
def slope_at(samples, i):
    return float(samples[i + 1]) - float(samples[i])
 
 
def find_best_loop_pair(samples, start_guess, end_guess, window, period,
                         sustain_start):
    start_candidates = find_rising_zero_crossings(samples, start_guess, window, sustain_start)
    end_candidates = find_rising_zero_crossings(samples, end_guess, window, sustain_start)
 
    if not start_candidates:
        raise ValueError(
            f"No rising zero crossings found near start={start_guess} "
            f"(window={window}, sustain_start={sustain_start}). Try a larger --window."
        )
    if not end_candidates:
        raise ValueError(
            f"No rising zero crossings found near end={end_guess} "
            f"(window={window}, sustain_start={sustain_start}). Try a larger --window."
        )
 
    best_pair = None
    best_score = float('inf')
    best_debug = None
 
    for s in start_candidates:
        for e in end_candidates:
            if e <= s:
                continue
            loop_len = e - s
            n_periods = round(loop_len / period)
            if n_periods < 1:
                continue
            ideal_len = n_periods * period
            period_error = abs(loop_len - ideal_len)
            slope_error = abs(slope_at(samples, s) - slope_at(samples, e))
 
            # Normalize slope error roughly against signal amplitude so the
            # two error terms are comparable in scale.
            amp_ref = max(1.0, np.abs(samples[max(0, s - 500):s + 500]).mean())
            norm_slope_error = slope_error / amp_ref
 
            score = period_error * 1.0 + norm_slope_error * 50.0
 
            if score < best_score:
                best_score = score
                best_pair = (s, e)
                best_debug = (period_error, norm_slope_error, n_periods)
 
    return best_pair, best_score, best_debug
 
 
def main():
    parser = argparse.ArgumentParser(description="Find seamless loop points in a sustained note WAV.")
    parser.add_argument("wav_path", help="Path to the source WAV file")
    parser.add_argument("--rate", type=int, default=16000, help="Target sample rate (must match your firmware pipeline)")
    parser.add_argument("--start", type=int, default=12000, help="Rough LOOP_START guess (samples)")
    parser.add_argument("--end", type=int, default=20000, help="Rough LOOP_END guess (samples)")
    parser.add_argument("--window", type=int, default=200, help="Search window (+/- samples) around each guess")
    parser.add_argument("--period-center", type=int, default=None,
                         help="Sample index to estimate pitch period from (default: midpoint of start/end)")
    args = parser.parse_args()
 
    print(f"Loading and resampling {args.wav_path} to {args.rate} Hz mono 32-bit PCM...")
    samples = load_samples(args.wav_path, args.rate)
    print(f"Loaded {len(samples)} samples ({len(samples) / args.rate:.2f} s)")
 
    sustain_start = find_sustain_start(samples)
    print(f"Detected attack ends / sustain begins around sample {sustain_start} "
          f"({sustain_start / args.rate * 1000:.1f} ms)")
 
    if args.start < sustain_start:
        print(f"WARNING: --start ({args.start}) is before detected sustain start "
              f"({sustain_start}). Candidates before sustain_start will be excluded; "
              f"consider raising --start.")
 
    period_center = args.period_center if args.period_center is not None else (args.start + args.end) // 2
    period = estimate_period(samples, period_center, args.rate)
    freq = args.rate / period
    print(f"Estimated fundamental period: {period} samples (~{freq:.1f} Hz)")
 
    best_pair, best_score, debug = find_best_loop_pair(
        samples, args.start, args.end, args.window, period, sustain_start
    )
 
    if best_pair is None:
        print("No valid loop pair found. Try widening --window or adjusting --start/--end.")
        sys.exit(1)
 
    s, e = best_pair
    period_error, norm_slope_error, n_periods = debug
    print()
    print("=== Best loop points found ===")
    print(f"LOOP_START = {s}")
    print(f"LOOP_END   = {e}")
    print(f"Loop length: {e - s} samples ({(e - s) / args.rate * 1000:.1f} ms, ~{n_periods} periods of {period} samples)")
    print(f"Period alignment error: {period_error:.1f} samples")
    print(f"Normalized slope mismatch: {norm_slope_error:.4f}")
    print()
    print("Drop these into your firmware:")
    print(f"const int LOOP_START = {s};")
    print(f"const int LOOP_END   = {e};")
 
 
if __name__ == "__main__":
    main()