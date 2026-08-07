#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


CHORD_ROOTS = ("C", "Cs", "D", "Eb", "E", "F", "Fs", "G", "Ab", "A", "Bb", "B")
CHORD_TEMPLATES = (
    ((0, 3, 7, 10), "_m"),
    ((0, 4, 7, 10), ""),
    ((0, 5, 7), "_sus4"),
    ((0, 3, 6), "_dim"),
    ((0, 3, 7), "_m"),
    ((0, 4, 7), ""),
)


def pitch_octave(pitch: str) -> int:
    return int(pitch[-1])


def stabilize_isolated_octaves(notes: list[dict], maximum_gap: float = 0.6) -> int:
    changed = 0
    for index in range(1, len(notes) - 1):
        previous = notes[index - 1]
        current = notes[index]
        following = notes[index + 1]
        target_octave = pitch_octave(previous["pitch"])
        if (
            target_octave == pitch_octave(following["pitch"])
            and pitch_octave(current["pitch"]) != target_octave
            and current["start_seconds"] - previous["start_seconds"] <= maximum_gap
            and following["start_seconds"] - current["start_seconds"] <= maximum_gap
        ):
            current["pitch"] = current["pitch"][:-1] + str(target_octave)
            changed += 1
    return changed


def infer_chord(active_notes: list[dict]) -> str:
    pitch_classes: dict[int, float] = {}
    for note in active_notes:
        pitch_class = note["midi"] % 12
        pitch_classes[pitch_class] = max(pitch_classes.get(pitch_class, 0.0), note["confidence"])

    best: tuple[float, int, str] | None = None
    for root in range(12):
        for intervals, suffix in CHORD_TEMPLATES:
            matched = [interval for interval in intervals if (root + interval) % 12 in pitch_classes]
            if len(matched) < 3:
                continue
            missing = len(intervals) - len(matched)
            extras = sum(
                1 for pitch_class in pitch_classes
                if all((root + interval) % 12 != pitch_class for interval in intervals)
            )
            score = sum(pitch_classes[(root + interval) % 12] for interval in matched)
            score -= missing * 0.65 + extras * 0.1
            if root in pitch_classes:
                score += 0.2
            if best is None or score > best[0]:
                best = (score, root, suffix)
    if best is None or best[0] < 1.25:
        return ""
    return f"pca_{CHORD_ROOTS[best[1]]}{best[2]}"


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a playable chart from a piano transcription")
    parser.add_argument("transcription", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--confidence", type=float, default=0.5)
    parser.add_argument("--onset-cluster", type=float, default=0.035)
    parser.add_argument("--minimum-spacing", type=float, default=0.25)
    parser.add_argument("--chord-confidence", type=float, default=0.4)
    parser.add_argument("--chord-spacing", type=float, default=1.5)
    args = parser.parse_args()

    transcript = json.loads(args.transcription.read_text(encoding="utf-8"))
    source_notes = [
        note for note in transcript["notes"]
        if note["confidence"] >= args.confidence
    ]

    # Basic Pitch can report a piano fundamental and one of its octave harmonics
    # as simultaneous notes. A gameplay prompt represents the strongest detected
    # key attack in that cluster; the complete polyphonic result remains preserved
    # in the transcription artifact.
    onset_clusters: list[list[dict]] = []
    for note in source_notes:
        if (
            not onset_clusters
            or note["start_seconds"] - onset_clusters[-1][0]["start_seconds"] > args.onset_cluster
        ):
            onset_clusters.append([note])
        else:
            onset_clusters[-1].append(note)
    attacks = [max(cluster, key=lambda note: note["confidence"]) for cluster in onset_clusters]

    selected: list[dict] = []
    for note in attacks:
        if (
            not selected
            or note["start_seconds"] - selected[-1]["start_seconds"] >= args.minimum_spacing
        ):
            selected.append(dict(note))

    octave_fixes = stabilize_isolated_octaves(selected)
    last_chord_time = -args.chord_spacing
    chord_ids: list[str] = []
    for note in selected:
        chord_id = ""
        if note["start_seconds"] - last_chord_time >= args.chord_spacing:
            active_notes = [
                source for source in transcript["notes"]
                if source["confidence"] >= args.chord_confidence
                and source["start_seconds"] <= note["start_seconds"] + 0.03
                and source["end_seconds"] >= note["start_seconds"] - 0.03
            ]
            chord_id = infer_chord(active_notes)
            if chord_id:
                last_chord_time = note["start_seconds"]
        chord_ids.append(chord_id)

    bpm = 120.0
    notes = []
    for note, chord_id in zip(selected, chord_ids):
        output_note = {
            "beat": round(note["start_seconds"] * bpm / 60.0, 6),
            "duration_beats": 0.25,
            "pitch": note["pitch"],
        }
        if chord_id:
            output_note["chord_id"] = chord_id
        notes.append(output_note)
    song = {
        "schema": "ff7rpianosongs.song.v2",
        "title": "Custom Piano Song",
        "bpm": bpm,
        "difficulty": 5,
        "score_thresholds": [0, 30000, 60000, 90000],
        "mode_change_combo_counts": [30, 60],
        "notes": notes,
    }
    args.output.write_text(json.dumps(song, indent=2) + "\n", encoding="utf-8")
    print(
        f"Gameplay chart: {len(notes)}/{len(transcript['notes'])} notes, "
        f"chords={sum(bool(chord_id) for chord_id in chord_ids)}, octave_fixes={octave_fixes}, "
        f"confidence>={args.confidence:.2f}, spacing>={args.minimum_spacing:.3f}s"
    )


if __name__ == "__main__":
    main()
