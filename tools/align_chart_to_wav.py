#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import find_peaks


PITCH_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


def read_mono_wav(path: Path) -> tuple[int, np.ndarray]:
    sample_rate, samples = wavfile.read(path)
    samples = samples.astype(np.float64)
    if samples.ndim == 2:
        samples = samples.mean(axis=1)
    peak = np.max(np.abs(samples))
    if not np.isfinite(peak) or peak <= 0:
        raise ValueError("WAV contains no usable audio")
    return sample_rate, samples / peak


def onset_flux(samples: np.ndarray, sample_rate: int) -> tuple[np.ndarray, int]:
    hop = max(1, round(sample_rate * 0.01))
    window_size = 2048
    if samples.size < window_size:
        raise ValueError("WAV is too short for onset analysis")
    frames = np.lib.stride_tricks.sliding_window_view(samples, window_size)[::hop]
    magnitudes = np.abs(np.fft.rfft(frames * np.hanning(window_size), n=4096, axis=1))
    frequencies = np.fft.rfftfreq(4096, 1.0 / sample_rate)
    band = (frequencies >= 70.0) & (frequencies <= 5000.0)
    log_magnitudes = np.log1p(20.0 * magnitudes[:, band])
    flux = np.concatenate(([0.0], np.maximum(0.0, np.diff(log_magnitudes, axis=0)).sum(axis=1)))
    deviation = np.std(flux)
    if not np.isfinite(deviation) or deviation <= 0:
        raise ValueError("WAV has no detectable spectral changes")
    return (flux - np.median(flux)) / deviation, hop


def select_onsets(flux: np.ndarray, hop: int, sample_rate: int, minimum_spacing: float) -> list[int]:
    candidates, _ = find_peaks(flux, distance=5, prominence=1.0)
    selected: list[int] = []
    for candidate in candidates[np.argsort(flux[candidates])[::-1]]:
        candidate_time = candidate * hop / sample_rate
        if all(abs(candidate_time - existing * hop / sample_rate) >= minimum_spacing for existing in selected):
            selected.append(int(candidate))
    return sorted(selected)


def onset_pitch(samples: np.ndarray, sample_rate: int, sample_index: int) -> str:
    fft_size = 16384
    before = samples[max(0, sample_index - fft_size):sample_index]
    after = samples[sample_index:min(samples.size, sample_index + fft_size)]
    before = np.pad(before, (fft_size - before.size, 0))
    after = np.pad(after, (0, fft_size - after.size))
    window = np.hanning(fft_size)
    novelty = np.maximum(
        0.0,
        np.abs(np.fft.rfft(after * window)) - np.abs(np.fft.rfft(before * window)),
    )
    frequencies = np.fft.rfftfreq(fft_size, 1.0 / sample_rate)

    best_score = -1.0
    best_midi = 60
    for midi in range(48, 85):  # C3 through C6 covers both minigame hands.
        fundamental = 440.0 * 2.0 ** ((midi - 69) / 12.0)
        score = 0.0
        for harmonic, weight in ((1, 1.0), (2, 0.4), (3, 0.2)):
            bin_index = int(np.argmin(np.abs(frequencies - fundamental * harmonic)))
            score += weight * np.max(novelty[max(0, bin_index - 1):bin_index + 2])
        if score > best_score:
            best_score = score
            best_midi = midi

    return f"{PITCH_NAMES[best_midi % 12]}{best_midi // 12 - 1}"


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate playable FF7R prompts from WAV note attacks")
    parser.add_argument("wav", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--minimum-spacing", type=float, default=0.45)
    args = parser.parse_args()
    if args.minimum_spacing <= 0:
        parser.error("--minimum-spacing must be positive")

    sample_rate, samples = read_mono_wav(args.wav)
    flux, hop = onset_flux(samples, sample_rate)
    onsets = select_onsets(flux, hop, sample_rate, args.minimum_spacing)
    bpm = 120.0
    notes = []
    for onset in onsets:
        seconds = onset * hop / sample_rate
        notes.append({
            "beat": round(seconds * bpm / 60.0, 6),
            "duration_beats": 0.25,
            "pitch": onset_pitch(samples, sample_rate, onset * hop),
        })

    song = {
        "schema": "ff7rpianosongs.song.v2",
        "title": "Custom Piano Song",
        "bpm": bpm,
        "difficulty": 5,
        "score_thresholds": [0, 20000, 40000, 60000],
        "mode_change_combo_counts": [25, 50],
        "notes": notes,
    }
    args.output.write_text(json.dumps(song, indent=2) + "\n", encoding="utf-8")
    print(
        f"Audio-aligned chart: {len(notes)} notes, "
        f"first={notes[0]['beat'] * 60.0 / bpm:.3f}s, "
        f"last={notes[-1]['beat'] * 60.0 / bpm:.3f}s"
    )


if __name__ == "__main__":
    main()
