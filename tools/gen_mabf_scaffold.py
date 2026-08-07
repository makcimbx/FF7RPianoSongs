#!/usr/bin/env python3
"""Generate the embedded FF7R piano MABF scaffold header.

The proven runtime-audio sidecar is a 0x1e66a0 byte MABF container with three
HCA slots. Each slot stores a 663682-byte HCA followed by a 62-byte non-zero
trailer. The release DLL only needs to embed the non-HCA scaffold: the 0x460
byte header and the three 62-byte trailers. User audio fills each HCA slot.
"""

from __future__ import annotations

import argparse
from pathlib import Path


SLOT_OFFSETS = (0x460, 0xA2520, 0x1445E0)
HCA_SIZE = 663_682
TRAILER_SIZE = 62
CONTAINER_SIZE = 0x1E66A0


def bytes_literal(data: bytes, indent: str = "    ") -> str:
    parts = [f"0x{b:02x}" for b in data]
    lines = []
    for i in range(0, len(parts), 12):
        lines.append(indent + ", ".join(parts[i : i + 12]) + ",")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="proven FF7RNativePoc.bgm09_twinkle_mabf.bin")
    parser.add_argument("output", type=Path, help="generated mabf_scaffold.h")
    args = parser.parse_args()

    blob = args.input.read_bytes()
    if len(blob) != CONTAINER_SIZE:
        raise SystemExit(f"unexpected MABF size: got {len(blob)}, expected {CONTAINER_SIZE}")

    header = blob[: SLOT_OFFSETS[0]]
    trailers = []
    for offset in SLOT_OFFSETS:
        start = offset + HCA_SIZE
        trailer = blob[start : start + TRAILER_SIZE]
        if len(trailer) != TRAILER_SIZE or not any(trailer):
            raise SystemExit(f"invalid or zero trailer at 0x{start:x}")
        trailers.append(trailer)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "#pragma once\n"
        "\n"
        "#include <array>\n"
        "#include <cstdint>\n"
        "\n"
        "namespace ff7r::piano::pipeline::mabf_scaffold {\n"
        "\n"
        "inline constexpr std::array<uint8_t, 0x460> kHeader = {\n"
        f"{bytes_literal(header)}\n"
        "};\n"
        "\n"
        "inline constexpr std::array<std::array<uint8_t, 62>, 3> kTrailers = {{\n"
        + "\n".join(
            "    {\n" + bytes_literal(trailer, "        ") + "\n    }," for trailer in trailers
        )
        + "\n}};\n"
        "\n"
        "} // namespace ff7r::piano::pipeline::mabf_scaffold\n",
        encoding="ascii",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
