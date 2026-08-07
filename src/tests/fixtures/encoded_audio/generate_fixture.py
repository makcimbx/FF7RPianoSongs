#!/usr/bin/env python3
import math
import struct
import wave
from pathlib import Path

rate = 48_000
root = Path(__file__).resolve().parent
with wave.open(str(root / "source.wav"), "wb") as output:
    output.setnchannels(2); output.setsampwidth(2); output.setframerate(rate)
    frames = bytearray()
    for index in range(rate):
        t = index / rate
        envelope = 0.75 if 0.10 <= t < 0.90 else 0.0
        left = envelope * (0.55 * math.sin(2 * math.pi * 440 * t) + 0.20 * math.sin(2 * math.pi * 880 * t))
        right = envelope * (0.50 * math.sin(2 * math.pi * 660 * t) + 0.15 * math.sin(2 * math.pi * 1320 * t))
        if index == 24_000: left, right = 0.95, -0.95
        frames += struct.pack("<hh", round(left * 32767), round(right * 32767))
    output.writeframes(frames)
