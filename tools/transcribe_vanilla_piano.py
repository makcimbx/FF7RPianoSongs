#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

from basic_pitch import ICASSP_2022_MODEL_PATH
from basic_pitch.inference import Model

from transcribe_piano_wav import transcribe


def main() -> None:
    parser = argparse.ArgumentParser(description="Transcribe FF7 Rebirth left/right piano stems")
    parser.add_argument("workspace", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    model = Model(ICASSP_2022_MODEL_PATH)
    for song_index in range(1, 9):
        song_id = f"bgm_piano_{song_index:02d}"
        project = args.workspace / f"{song_id}_Project"
        for stem, role in (("001", "chord"), ("002", "monotone")):
            wav = project / f"{song_id}_{stem}.wav"
            if not wav.is_file():
                raise FileNotFoundError(wav)
            notes = transcribe(wav, model=model)
            result = {
                "schema": "ff7rpianosongs.transcription.v1",
                "source": str(wav),
                "model": "spotify/basic-pitch",
                "song_id": song_id,
                "stem": stem,
                "role": role,
                "parameters": {
                    "onset_threshold": 0.5,
                    "frame_threshold": 0.3,
                    "minimum_note_length_ms": 80.0,
                    "minimum_midi": 24,
                    "maximum_midi": 96,
                },
                "notes": notes,
            }
            destination = args.output / f"{song_id}_{stem}.json"
            destination.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            print(f"{song_id} {role}: {len(notes)} notes")


if __name__ == "__main__":
    main()
