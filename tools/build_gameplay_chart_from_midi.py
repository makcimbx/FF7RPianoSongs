#!/usr/bin/env python3

import argparse
import bisect
import json
import math
import subprocess
from pathlib import Path

import numpy as np
import pretty_midi


NATIVE_CHORD_ROOTS = ("C", "Cs", "D", "Eb", "E", "F", "Fs", "G", "Ab", "A", "Bb", "B")
CHORD_TEMPLATES = (
    ((0, 4, 7, 11), "_Maj7"),
    ((0, 4, 7, 10), "_7"),
    ((0, 3, 7, 10), "_m7"),
    ((0, 4, 7), ""),
    ((0, 3, 7), "_m"),
    ((0, 3, 6), "_dim"),
    ((0, 5, 7), "_sus4"),
)


def stabilize_isolated_octaves(notes: list[dict], maximum_gap: float = 0.65) -> int:
    changed = 0
    for index in range(1, len(notes) - 1):
        previous = notes[index - 1]
        current = notes[index]
        following = notes[index + 1]
        previous_octave = previous["midi"] // 12
        current_octave = current["midi"] // 12
        following_octave = following["midi"] // 12
        if (
            previous_octave == following_octave
            and current_octave != previous_octave
            and current["start"] - previous["start"] <= maximum_gap
            and following["start"] - current["start"] <= maximum_gap
        ):
            adjusted = current["midi"] + (previous_octave - current_octave) * 12
            if 24 <= adjusted <= 96:
                current["midi"] = adjusted
                changed += 1
    return changed


def infer_chord_id(root: int, active_pitches: set[int]) -> str | None:
    intervals = {(pitch - root) % 12 for pitch in active_pitches}
    for template, suffix in CHORD_TEMPLATES:
        if set(template).issubset(intervals):
            return f"pca_{NATIVE_CHORD_ROOTS[root]}{suffix}"
    return None


def group_onsets(notes: list[pretty_midi.Note]) -> dict[float, list[pretty_midi.Note]]:
    groups: dict[float, list[pretty_midi.Note]] = {}
    for note in notes:
        groups.setdefault(round(note.start, 6), []).append(note)
    return groups


def measure_audio_prominence(audio_path: Path, attacks: list[dict], alignment: float) -> None:
    sample_rate = 22050
    decoded = subprocess.run(
        [
            "ffmpeg", "-v", "error", "-i", str(audio_path),
            "-f", "f32le", "-ac", "1", "-ar", str(sample_rate), "-",
        ],
        check=True,
        capture_output=True,
    ).stdout
    audio = np.frombuffer(decoded, dtype="<f4")
    fft_size = 4096
    window_frames = round(0.055 * sample_rate)
    window = np.hanning(window_frames).astype(np.float32)
    scores = []
    for note in attacks:
        center = round((note["start"] + alignment) * sample_rate)
        before = audio[max(0, center - window_frames):center]
        after = audio[center:min(len(audio), center + window_frames)]
        if len(before) != window_frames or len(after) != window_frames:
            scores.append(0.0)
            continue
        before_spectrum = np.abs(np.fft.rfft(before * window, n=fft_size))
        after_spectrum = np.abs(np.fft.rfft(after * window, n=fft_size))
        novelty = np.maximum(after_spectrum - before_spectrum, 0.0)
        fundamental = 440.0 * 2.0 ** ((note["midi"] - 69) / 12.0)
        score = 0.0
        for harmonic, weight in ((1, 1.0), (2, 0.45), (3, 0.25)):
            bin_index = round(fundamental * harmonic * fft_size / sample_rate)
            if 1 <= bin_index < len(novelty) - 1:
                score += weight * float(np.max(novelty[bin_index - 1:bin_index + 2]))
        scores.append(score)

    scale = float(np.percentile(scores, 90)) if scores else 0.0
    for note, score in zip(attacks, scores):
        note["audio_prominence"] = min(score / scale, 2.0) if scale > 0 else 0.0


def attack_score(note: dict) -> float:
    duration = min(note["end"] - note["start"], 1.0)
    score = 2.0 * note["velocity"] / 127.0 + duration + 2.5 * note.get("audio_prominence", 0.0)
    if abs(note["start"] / 2.0 - round(note["start"] / 2.0)) < 0.02:
        score += 0.6
    elif abs(note["start"] / 0.5 - round(note["start"] / 0.5)) < 0.02:
        score += 0.4
    elif abs(note["start"] / 0.25 - round(note["start"] / 0.25)) < 0.02:
        score += 0.15
    return score


def select_accented_attacks(attacks: list[dict], count: int, minimum_spacing: float) -> list[dict]:
    if count <= 0:
        selected = []
        for note in attacks:
            if not selected or note["start"] - selected[-1]["start"] >= minimum_spacing - 1e-6:
                selected.append(note)
        return selected

    starts = [note["start"] for note in attacks]
    predecessors = [
        bisect.bisect_right(starts, start - minimum_spacing + 1e-6, 0, index) - 1
        for index, start in enumerate(starts)
    ]
    impossible = float("-inf")
    scores = [[impossible] * (len(attacks) + 1) for _ in range(count + 1)]
    choices = [[False] * (len(attacks) + 1) for _ in range(count + 1)]
    for index in range(len(attacks) + 1):
        scores[0][index] = 0.0

    for selected_count in range(1, count + 1):
        for prefix_count in range(1, len(attacks) + 1):
            scores[selected_count][prefix_count] = scores[selected_count][prefix_count - 1]
            note_index = prefix_count - 1
            previous_prefix = predecessors[note_index] + 1
            previous_score = scores[selected_count - 1][previous_prefix]
            candidate = previous_score + attack_score(attacks[note_index])
            if previous_score != impossible and candidate > scores[selected_count][prefix_count]:
                scores[selected_count][prefix_count] = candidate
                choices[selected_count][prefix_count] = True

    if scores[count][len(attacks)] == impossible:
        raise ValueError(f"cannot select {count} prompts with {minimum_spacing:.3f}s spacing")

    selected = []
    selected_count = count
    prefix_count = len(attacks)
    while selected_count:
        if choices[selected_count][prefix_count]:
            note_index = prefix_count - 1
            selected.append(attacks[note_index])
            prefix_count = predecessors[note_index] + 1
            selected_count -= 1
        else:
            prefix_count -= 1
    selected.reverse()
    return selected


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a playable two-hand chart from a matching MIDI")
    parser.add_argument("midi", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--audio", type=Path)
    parser.add_argument("--title", default="Custom Piano Song")
    parser.add_argument("--difficulty", type=int, default=4)
    parser.add_argument("--hand-split-midi", type=int, default=60, help="median-pitch split used to classify arbitrary instrument tracks")
    parser.add_argument("--audio-offset", type=float, default=0.035)
    parser.add_argument("--audio-alignment", type=float, default=0.031)
    parser.add_argument("--minimum-spacing", type=float, default=0.5)
    parser.add_argument("--prompt-count", type=int, default=180)
    parser.add_argument("--chord-minimum-spacing", type=float, default=2.0)
    parser.add_argument("--chord-count", type=int, default=20)
    parser.add_argument("--fallback-minimum-spacing", type=float, default=2.0)
    parser.add_argument("--fallback-silence", type=float, default=1.0)
    args = parser.parse_args()

    midi = pretty_midi.PrettyMIDI(str(args.midi))
    instruments = [instrument for instrument in midi.instruments if instrument.notes and not instrument.is_drum]
    if not instruments:
        parser.error("MIDI does not contain any pitched instrument tracks")
    medians = {id(instrument): float(np.median([note.pitch for note in instrument.notes])) for instrument in instruments}
    melody_instruments = [instrument for instrument in instruments if medians[id(instrument)] >= args.hand_split_midi]
    harmony_instruments = [instrument for instrument in instruments if medians[id(instrument)] < args.hand_split_midi]
    if not melody_instruments and len(instruments) > 1:
        melody_instruments = [max(instruments, key=lambda instrument: medians[id(instrument)])]
        harmony_instruments = [instrument for instrument in instruments if instrument not in melody_instruments]
    elif not melody_instruments:
        melody_instruments = instruments
    if not harmony_instruments and len(instruments) > 1:
        harmony_instruments = [min(instruments, key=lambda instrument: medians[id(instrument)])]
        melody_instruments = [instrument for instrument in instruments if instrument not in harmony_instruments]

    melody = [note for instrument in melody_instruments for note in instrument.notes]
    harmony = [note for instrument in harmony_instruments for note in instrument.notes]
    all_notes = [note for instrument in instruments for note in instrument.notes]
    onset_groups = group_onsets(melody)

    # A right-stick prompt is monophonic. When the MIDI right hand contains a
    # two-note voicing, retain its top voice instead of creating simultaneous
    # prompts or guessing from the rendered spectrum.
    attacks = []
    for start, notes in sorted(onset_groups.items()):
        lead = max(notes, key=lambda note: (note.pitch, note.velocity))
        attacks.append({
            "start": start,
            "end": lead.end,
            "midi": lead.pitch,
            "velocity": lead.velocity,
        })

    if args.audio:
        measure_audio_prominence(args.audio, attacks, args.audio_alignment)
    selected = select_accented_attacks(attacks, args.prompt_count, args.minimum_spacing)
    selected_prominence = (
        sum(note.get("audio_prominence", 0.0) for note in selected) / len(selected)
        if selected else 0.0
    )
    octave_fixes = stabilize_isolated_octaves(selected)

    # A monophonic low-register track does not contain enough information to
    # invent a major/minor chord. During primary-track rests, preserve that MIDI
    # material as sparse exact-pitch prompts instead.
    melody_starts = sorted(onset_groups)
    fallback_attacks = []
    for start, notes in sorted(group_onsets(harmony).items()):
        position = bisect.bisect_left(melody_starts, start)
        distances = []
        if position:
            distances.append(start - melody_starts[position - 1])
        if position < len(melody_starts):
            distances.append(melody_starts[position] - start)
        if distances and min(distances) >= args.fallback_silence:
            lead = max(notes, key=lambda note: (note.pitch, note.velocity))
            fallback_attacks.append({
                "start": start,
                "end": lead.end,
                "midi": lead.pitch,
                "velocity": lead.velocity,
                "fallback": True,
            })
    if args.audio:
        measure_audio_prominence(args.audio, fallback_attacks, args.audio_alignment)
    selected_fallback = select_accented_attacks(
        fallback_attacks, 0, args.fallback_minimum_spacing)
    selected = sorted(selected + selected_fallback, key=lambda note: note["start"])

    harmony_attacks = []
    for start, notes in sorted(group_onsets(harmony).items()):
        root_note = min(notes, key=lambda note: (note.pitch, -note.velocity))
        active_pitches = {
            note.pitch % 12 for note in all_notes
            if note.start <= start + 0.04 and note.end >= start - 0.04
        }
        chord_id = infer_chord_id(root_note.pitch % 12, active_pitches)
        if chord_id:
            harmony_attacks.append({
                "start": start,
                "end": max(note.end for note in notes),
                "midi": root_note.pitch,
                "velocity": max(note.velocity for note in notes),
                "chord_id": chord_id,
            })
    if args.audio:
        measure_audio_prominence(args.audio, harmony_attacks, args.audio_alignment)
    selected_chords = select_accented_attacks(
        harmony_attacks, args.chord_count, args.chord_minimum_spacing)

    output_by_centisecond: dict[int, dict] = {}
    for note in selected:
        centisecond = math.floor((note["start"] + args.audio_offset) * 100.0 + 0.5)
        output_by_centisecond[centisecond] = {
            "beat": centisecond / 50.0,
            "duration_beats": 0.25,
            "pitch": pretty_midi.note_number_to_name(note["midi"]),
        }
    for note in selected_chords:
        centisecond = math.floor((note["start"] + args.audio_offset) * 100.0 + 0.5)
        output_note = output_by_centisecond.setdefault(centisecond, {
            "beat": centisecond / 50.0,
            "duration_beats": 0.25,
        })
        output_note["chord_id"] = note["chord_id"]
    output_notes = [output_by_centisecond[key] for key in sorted(output_by_centisecond)]

    song = {
        "schema": "ff7rpianosongs.song.v2",
        "title": args.title,
        "bpm": 120.0,
        "difficulty": args.difficulty,
        "score_thresholds": [0, 40000, 80000, 120000],
        "mode_change_combo_counts": [40, 80],
        "notes": output_notes,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(song, indent=2) + "\n", encoding="utf-8")
    print(
        f"MIDI gameplay chart: {len(output_notes)}/{len(attacks)} melody onsets, "
        f"right={len(selected)}, left={len(selected_chords)}, merged={len(selected) + len(selected_chords) - len(output_notes)}, "
        f"tracks={len(instruments)} ({len(melody_instruments)} right/{len(harmony_instruments)} left), octave_fixes={octave_fixes}, "
        f"fallback={len(selected_fallback)}, exact_chord_candidates={len(harmony_attacks)}, "
        f"offset={args.audio_offset:+.3f}s, spacing={args.minimum_spacing:.3f}s, "
        f"selection={'audio-accent-aware' if args.audio else 'accent-aware'}, "
        f"mean_audio_prominence={selected_prominence:.3f}"
    )


if __name__ == "__main__":
    main()
