#!/usr/bin/env python3

import argparse
import json
import re
import subprocess
from pathlib import Path

import numpy as np
from scipy.io import wavfile


PITCH_CLASS = {
    "C": 0,
    "C#": 1,
    "Db": 1,
    "D": 2,
    "D#": 3,
    "Eb": 3,
    "E": 4,
    "F": 5,
    "F#": 6,
    "Gb": 6,
    "G": 7,
    "G#": 8,
    "Ab": 8,
    "A": 9,
    "A#": 10,
    "Bb": 10,
    "B": 11,
}


def midi_for_pitch(pitch: str) -> int:
    match = re.fullmatch(r"([A-G](?:#|b)?)(-?\d+)", pitch)
    if not match or match.group(1) not in PITCH_CLASS:
        raise ValueError(f"invalid pitch: {pitch}")
    return (int(match.group(2)) + 1) * 12 + PITCH_CLASS[match.group(1)]


def main() -> None:
    parser = argparse.ArgumentParser(description="Render stereo audio/chart timing references")
    parser.add_argument("audio", type=Path)
    parser.add_argument("song_json", type=Path)
    parser.add_argument("output_directory", type=Path)
    parser.add_argument("--sample-rate", type=int, default=22050)
    parser.add_argument("--segment-seconds", type=float, default=24.0)
    parser.add_argument("--segment-starts", type=float, nargs="+", default=[0.0, 64.0, 132.0])
    parser.add_argument("--playback-delay", type=float, default=0.007)
    args = parser.parse_args()

    command = [
        "ffmpeg", "-v", "error", "-i", str(args.audio),
        "-f", "f32le", "-ac", "1", "-ar", str(args.sample_rate), "-",
    ]
    decoded = subprocess.run(command, check=True, capture_output=True).stdout
    source = np.frombuffer(decoded, dtype="<f4")
    delay_frames = round(args.playback_delay * args.sample_rate)
    if delay_frames:
        source = np.pad(source, (delay_frames, 0))

    song = json.loads(args.song_json.read_text(encoding="utf-8"))
    bpm = float(song["bpm"])
    prompts = [
        (float(note["beat"]) * 60.0 / bpm, midi_for_pitch(note["pitch"]))
        for note in song["notes"]
    ]
    args.output_directory.mkdir(parents=True, exist_ok=True)

    tone_frames = round(0.09 * args.sample_rate)
    relative_time = np.arange(tone_frames, dtype=np.float32) / args.sample_rate
    envelope = np.exp(-relative_time * 28.0).astype(np.float32)
    segment_frames = round(args.segment_seconds * args.sample_rate)

    for start_seconds in args.segment_starts:
        start_frame = round(start_seconds * args.sample_rate)
        left = np.zeros(segment_frames, dtype=np.float32)
        available = source[start_frame:start_frame + segment_frames]
        left[:len(available)] = available
        peak = float(np.max(np.abs(left)))
        if peak > 0:
            left *= 0.8 / peak

        right = np.zeros(segment_frames, dtype=np.float32)
        for prompt_seconds, midi in prompts:
            relative_frame = round((prompt_seconds - start_seconds) * args.sample_rate)
            if relative_frame < 0 or relative_frame >= segment_frames:
                continue
            count = min(tone_frames, segment_frames - relative_frame)
            frequency = 440.0 * 2.0 ** ((midi - 69) / 12.0)
            tone = np.sin(2.0 * np.pi * frequency * relative_time[:count])
            right[relative_frame:relative_frame + count] += 0.65 * tone * envelope[:count]
        right = np.clip(right, -1.0, 1.0)

        stereo = np.column_stack((left, right))
        output = args.output_directory / f"timing_{start_seconds:06.1f}.wav"
        wavfile.write(output, args.sample_rate, np.round(stereo * 32767.0).astype(np.int16))
        print(output)


if __name__ == "__main__":
    main()
