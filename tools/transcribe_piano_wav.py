#!/usr/bin/env python3

import argparse
import json
import math
from pathlib import Path

from basic_pitch.inference import Model, predict


PITCH_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


def frequency_for_midi(midi: int) -> float:
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def pitch_name(midi: int) -> str:
    return f"{PITCH_NAMES[midi % 12]}{midi // 12 - 1}"


def transcribe(
    wav: Path,
    model: Model | None = None,
    onset_threshold: float = 0.5,
    frame_threshold: float = 0.3,
    minimum_note_length_ms: float = 80.0,
    minimum_midi: int = 24,
    maximum_midi: int = 96,
) -> list[dict]:
    predict_options = dict(
        onset_threshold=onset_threshold,
        frame_threshold=frame_threshold,
        minimum_note_length=minimum_note_length_ms,
        minimum_frequency=frequency_for_midi(minimum_midi),
        maximum_frequency=frequency_for_midi(maximum_midi),
        multiple_pitch_bends=False,
    )
    if model is not None:
        predict_options["model_or_model_path"] = model
    _, _, note_events = predict(wav, **predict_options)
    notes = []
    for start, end, midi, confidence, _ in sorted(note_events, key=lambda event: (event[0], event[2])):
        if not all(math.isfinite(value) for value in (start, end, confidence)):
            continue
        midi = int(midi)
        notes.append({
            "start_seconds": round(float(start), 6),
            "end_seconds": round(float(end), 6),
            "duration_seconds": round(float(end - start), 6),
            "midi": midi,
            "pitch": pitch_name(midi),
            "confidence": round(float(confidence), 6),
        })
    return notes


def main() -> None:
    parser = argparse.ArgumentParser(description="Transcribe every detected piano note in a WAV")
    parser.add_argument("wav", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--onset-threshold", type=float, default=0.5)
    parser.add_argument("--frame-threshold", type=float, default=0.3)
    parser.add_argument("--minimum-note-length-ms", type=float, default=80.0)
    parser.add_argument("--minimum-midi", type=int, default=24, help="C1 by default")
    parser.add_argument("--maximum-midi", type=int, default=96, help="C7 by default")
    args = parser.parse_args()

    if not args.wav.is_file():
        parser.error(f"WAV does not exist: {args.wav}")
    if args.minimum_midi > args.maximum_midi:
        parser.error("--minimum-midi cannot exceed --maximum-midi")

    notes = transcribe(
        args.wav,
        onset_threshold=args.onset_threshold,
        frame_threshold=args.frame_threshold,
        minimum_note_length_ms=args.minimum_note_length_ms,
        minimum_midi=args.minimum_midi,
        maximum_midi=args.maximum_midi,
    )

    transcript = {
        "schema": "ff7rpianosongs.transcription.v1",
        "source": str(args.wav),
        "model": "spotify/basic-pitch",
        "parameters": {
            "onset_threshold": args.onset_threshold,
            "frame_threshold": args.frame_threshold,
            "minimum_note_length_ms": args.minimum_note_length_ms,
            "minimum_midi": args.minimum_midi,
            "maximum_midi": args.maximum_midi,
        },
        "notes": notes,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(transcript, indent=2) + "\n", encoding="utf-8")
    print(f"Transcribed {len(notes)} notes to {args.output}")


if __name__ == "__main__":
    main()
