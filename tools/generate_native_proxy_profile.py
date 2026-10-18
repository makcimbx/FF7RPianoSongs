"""Emit the minimal, catalog-derived entry profile used before the ASI is loaded."""

import argparse
import json
from pathlib import Path


def render(catalog: dict, build_id: str) -> str:
    builds = [item for item in catalog["builds"] if item["id"] == build_id]
    addresses = [item for item in catalog["addresses"] if item["id"] == "native_audio_exe_entry"]
    if len(builds) != 1 or len(addresses) != 1:
        raise ValueError("Exactly one supported build and entry contract required")
    build = builds[0]
    entry = addresses[0]["builds"][build_id]
    code = bytes.fromhex(entry["signature"]["bytes"])
    if not 14 <= len(code) <= 32 or entry["signature"]["mask"] != "x" * len(code):
        raise ValueError("Entry requires a bounded exact signature")
    rva = int(entry["rva"], 0)
    image = int(build["size_of_image"], 0)
    if rva < 0x1000 or rva + len(code) > image:
        raise ValueError("Entry outside supported image")
    return f"""// Generated from src/game/rva_catalog.json; do not edit.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace ff7r::piano::proxy::generated {{
inline constexpr std::uint32_t kTimestamp = {build['pe_timestamp']};
inline constexpr std::uint32_t kImageSize = {build['size_of_image']};
inline constexpr std::uint32_t kChecksum = {build['pe_checksum']};
inline constexpr std::uintptr_t kEntryRva = {entry['rva']};
inline constexpr std::array<std::uint8_t, {len(code)}> kEntryBytes = {{{', '.join(hex(b) for b in code)}}};
inline constexpr std::string_view kBuildId = "{build_id}";
}} // namespace ff7r::piano::proxy::generated
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = render(json.loads(args.catalog.read_text(encoding="utf-8")), args.build)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != output:
        args.output.write_text(output, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
