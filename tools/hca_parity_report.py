#!/usr/bin/env python3
"""Hash/inspect HCA-in-MABF artifacts and compare decoded PCM deterministically."""

from __future__ import annotations

import argparse
import array
import hashlib
import json
import math
import pathlib
import struct
import sys
import wave


def crc16(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def crc_valid(data: bytes) -> bool:
    return len(data) >= 2 and crc16(data[:-2]) == int.from_bytes(data[-2:], "big")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inspect_mabf(path: pathlib.Path, offset: int, extract_dir: pathlib.Path | None) -> dict:
    source = path.read_bytes()
    if offset < 0 or offset > len(source):
        raise ValueError("MABF offset is outside the source file")
    mabf = source[offset:]
    if len(mabf) < 0x460 or mabf[:4] != b"mabf":
        raise ValueError("input does not contain a complete MABF header")
    if int.from_bytes(mabf[0x0C:0x10], "little") + 0x30 != len(mabf):
        raise ValueError("declared MABF container size does not match the input")
    profiles = (("standard-0x460", 0x440, 0x424, 0x428),
                ("extended-0x4f0", 0x4D0, 0x4B4, 0x4B8))
    layout_profile = None
    markers = []
    for profile, base, mode1_field, mode2_field in profiles:
        candidate = [base + 0x20,
                     base + int.from_bytes(mabf[mode1_field:mode1_field + 4], "little"),
                     base + int.from_bytes(mabf[mode2_field:mode2_field + 4], "little")]
        if all(mabf.startswith(b"HCA\0", marker) for marker in candidate):
            layout_profile, markers = profile, candidate
            break
    if layout_profile is None:
        raise ValueError("MABF does not match a supported native mode-layout profile")
    if markers != sorted(markers) or len(set(markers)) != 3 or markers[-1] >= len(mabf):
        raise ValueError("declared MABF mode offsets are invalid")
    if any(not mabf.startswith(b"HCA\0", marker) for marker in markers):
        raise ValueError("declared MABF mode offset does not contain HCA magic")
    modes = []
    if extract_dir:
        extract_dir.mkdir(parents=True, exist_ok=True)
    for mode, start in enumerate(markers):
        slot_end = markers[mode + 1] if mode < 2 else len(mabf)
        if start + 30 > slot_end:
            raise ValueError(f"mode {mode} HCA header is truncated")
        header_size = int.from_bytes(mabf[start + 6:start + 8], "big")
        channels = mabf[start + 12]
        sample_rate = int.from_bytes(mabf[start + 13:start + 16], "big")
        frame_count = int.from_bytes(mabf[start + 16:start + 20], "big")
        inserted = int.from_bytes(mabf[start + 20:start + 22], "big")
        appended = int.from_bytes(mabf[start + 22:start + 24], "big")
        block_size = int.from_bytes(mabf[start + 28:start + 30], "big")
        hca_size = header_size + frame_count * block_size
        if header_size < 32 or block_size < 2 or hca_size > slot_end - start:
            raise ValueError(f"mode {mode} declares invalid HCA geometry")
        hca = mabf[start:start + hca_size]
        trailer = mabf[start + hca_size:slot_end]
        prefix_size = len(trailer) - 46
        if not 1 <= prefix_size <= 16 or (slot_end - start) % 16 != 0:
            raise ValueError(f"mode {mode} has invalid MABF slot prefix/alignment")
        prefix, metadata_suffix = trailer[:prefix_size], trailer[prefix_size:]
        if mode < 2:
            prefix_zero_body = prefix_size >= 2 and not any(prefix[:-2])
            prefix_tail = prefix[-2:] if prefix_size >= 2 else prefix
            prefix_valid = prefix_size >= 2 and prefix_zero_body and prefix_tail == b"\x01\x00"
            prefix_class = "zero-body-tail-0100"
        else:
            prefix_zero_body = not any(prefix)
            prefix_tail = prefix[-2:] if prefix_size >= 2 else prefix
            prefix_valid = prefix_zero_body
            prefix_class = "all-zero"
        if not prefix_valid:
            raise ValueError(f"mode {mode} has invalid native MABF prefix semantics")
        frames = [hca[header_size + i * block_size:header_size + (i + 1) * block_size]
                  for i in range(frame_count)]
        chunks = [tag.decode("ascii") for tag in (b"fmt\0", b"comp", b"dec\0", b"vbr\0", b"ath\0",
                                                     b"loop", b"ciph", b"rva\0", b"comm", b"pad\0")
                  if tag in hca[:header_size]]
        cipher_offset = hca[:header_size].find(b"ciph")
        item = {
            "mode": mode,
            "mabf_offset": start,
            "hca_bytes": hca_size,
            "sha256": sha256(hca),
            "version": f"0x{int.from_bytes(hca[4:6], 'big'):04x}",
            "header_bytes": header_size,
            "channels": channels,
            "sample_rate": sample_rate,
            "frame_count": frame_count,
            "block_size": block_size,
            "inserted_samples": inserted,
            "appended_samples": appended,
            "logical_samples": frame_count * 1024 - inserted - appended,
            "chunks": chunks,
            "cipher_type": int.from_bytes(hca[cipher_offset + 4:cipher_offset + 6], "big")
            if cipher_offset >= 0 else None,
            "header_crc_valid": crc_valid(hca[:header_size]),
            "valid_frame_crcs": sum(crc_valid(frame) for frame in frames),
            "invalid_frame_crcs": sum(not crc_valid(frame) for frame in frames),
            "slot_bytes": slot_end - start,
            "slot_alignment": (slot_end - start) % 16,
            "mode_prefix_bytes": prefix_size,
            "mode_prefix_class": prefix_class,
            "mode_prefix_tail_hex": prefix_tail.hex(),
            "mode_prefix_zero_body": prefix_zero_body,
            "mode_prefix_valid": prefix_valid,
            "metadata_suffix_bytes": len(metadata_suffix),
            "metadata_suffix_sha256": sha256(metadata_suffix),
        }
        modes.append(item)
        if extract_dir:
            (extract_dir / f"mode{mode}.hca").write_bytes(hca)
    return {
        "source": str(path.resolve()),
        "source_bytes": len(source),
        "source_sha256": sha256(source),
        "mabf_offset": offset,
        "mabf_bytes": len(mabf),
        "mabf_sha256": sha256(mabf),
        "layout_profile": layout_profile,
        "mode_count": len(modes),
        "modes": modes,
    }


def read_pcm16(path: pathlib.Path) -> tuple[dict, array.array]:
    with wave.open(str(path), "rb") as wav:
        metadata = {"channels": wav.getnchannels(), "sample_rate": wav.getframerate(),
                    "sample_width": wav.getsampwidth(), "frames": wav.getnframes()}
        if metadata["sample_width"] != 2:
            raise ValueError(f"{path}: expected PCM16")
        samples = array.array("h", wav.readframes(metadata["frames"]))
    if sys.byteorder != "little":
        samples.byteswap()
    return metadata, samples


def spectral_probe(reference: array.array, decoded: array.array, channels: int, sample_rate: int) -> dict:
    frequencies = (125, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000, 20000)
    window = 2048
    frames = min(len(reference), len(decoded)) // channels
    starts = [round(i * (frames - window) / 7) for i in range(8)] if frames >= window else [0]
    differences = []
    for start in starts:
        for frequency in frequencies:
            omega = 2 * math.pi * frequency / sample_rate
            ref_real = ref_imag = dec_real = dec_imag = 0.0
            for index in range(window):
                frame = start + index
                if frame >= frames:
                    break
                ref_value = sum(reference[frame * channels + channel] for channel in range(channels)) / (channels * 32768.0)
                dec_value = sum(decoded[frame * channels + channel] for channel in range(channels)) / (channels * 32768.0)
                angle = omega * index
                cosine, sine = math.cos(angle), math.sin(angle)
                ref_real += ref_value * cosine
                ref_imag -= ref_value * sine
                dec_real += dec_value * cosine
                dec_imag -= dec_value * sine
            ref_magnitude = math.hypot(ref_real, ref_imag)
            dec_magnitude = math.hypot(dec_real, dec_imag)
            if ref_magnitude > 1e-5:
                differences.append(abs(20 * math.log10(max(dec_magnitude, 1e-12) / ref_magnitude)))
    return {"method": "8x2048-frame mono DFT probes at 10 fixed 125-20000 Hz frequencies",
            "bins_compared": len(differences),
            "mean_absolute_db": sum(differences) / len(differences) if differences else None,
            "max_absolute_db": max(differences) if differences else None}


def compare_wav(reference_path: pathlib.Path, decoded_path: pathlib.Path) -> dict:
    ref_meta, reference = read_pcm16(reference_path)
    dec_meta, decoded = read_pcm16(decoded_path)
    count = min(len(reference), len(decoded))
    sum_signal = sum_decoded = sum_cross = sum_error = 0.0
    peak_error = 0
    for ref, dec in zip(reference[:count], decoded[:count]):
        error = dec - ref
        sum_signal += ref * ref
        sum_decoded += dec * dec
        sum_cross += ref * dec
        sum_error += error * error
        peak_error = max(peak_error, abs(error))
    rms_signal = math.sqrt(sum_signal / count) if count else 0.0
    rms_decoded = math.sqrt(sum_decoded / count) if count else 0.0
    rms_error = math.sqrt(sum_error / count) if count else 0.0
    snr = None if rms_error == 0 else 20 * math.log10(rms_signal / rms_error)
    correlation = sum_cross / math.sqrt(sum_signal * sum_decoded) if sum_signal and sum_decoded else None
    return {
        "reference": str(reference_path.resolve()), "decoded": str(decoded_path.resolve()),
        "reference_metadata": ref_meta, "decoded_metadata": dec_meta,
        "sample_count_equal": len(reference) == len(decoded),
        "peak_error_pcm16": peak_error, "peak_error_full_scale": peak_error / 32768.0,
        "rms_error_pcm16": rms_error, "rms_error_full_scale": rms_error / 32768.0,
        "signal_rms_pcm16": rms_signal, "decoded_rms_pcm16": rms_decoded,
        "correlation": correlation, "snr_db": snr, "pcm_exact": rms_error == 0,
        "spectral_probe": spectral_probe(reference, decoded, ref_meta["channels"], ref_meta["sample_rate"]),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("input", type=pathlib.Path)
    inspect_parser.add_argument("--offset", type=lambda value: int(value, 0), default=0)
    inspect_parser.add_argument("--extract-dir", type=pathlib.Path)
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("reference", type=pathlib.Path)
    compare_parser.add_argument("decoded", type=pathlib.Path)
    args = parser.parse_args()
    result = inspect_mabf(args.input, args.offset, args.extract_dir) if args.command == "inspect" else \
        compare_wav(args.reference, args.decoded)
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
