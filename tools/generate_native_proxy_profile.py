"""Emit bounded entry/CALL transport admission, not native publication authority."""

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
    authority = build["runtime_profile"]["owned_proxy"]
    post = authority["phase"] == "pre_winmain"
    if not post and authority != {"phase":"entry", "call_id":None, "target_id":None, "patch_bytes":0}:
        raise ValueError("Unknown owned proxy phase")
    call_rva = target_rva = 0
    window = b""
    if post:
        if authority != {"phase":"pre_winmain", "call_id":"native_bootstrap_show_to_winmain", "target_id":"native_bootstrap_show", "patch_bytes":5}:
            raise ValueError("Unqualified held CALL mutation")
        def contract(name):
            rows = [row for row in catalog["addresses"] if row["id"] == name]
            if len(rows) != 1:
                raise ValueError("Unique CALL contract required")
            item = rows[0]["builds"][build_id]
            if not any(e["type"] == "static_disk_contract" for e in item["evidence"]):
                raise ValueError("Owned CALL requires matching-disk qualification")
            data = bytes.fromhex(item["signature"]["bytes"])
            if item["signature"]["mask"] != "x" * len(data):
                raise ValueError("Exact CALL/target signatures required")
            offset = int(item["rva"], 0)
            if offset < 0x1000 or offset + len(data) > image:
                raise ValueError("CALL/target outside image")
            return offset, data
        call_rva, window = contract(authority["call_id"])
        target_rva, target_bytes = contract(authority["target_id"])
        if not target_bytes or len(target_bytes) > 65536:
            raise ValueError("Bounded helper contract required")
        if not 5 <= len(window) <= 64 or window[0] != 0xe8 or call_rva + 5 + int.from_bytes(window[1:5], "little", signed=True) != target_rva:
            raise ValueError("CALL target mismatch")
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
inline constexpr bool kOwnedCallEnabled = {'true' if post else 'false'};
inline constexpr std::uint32_t kCallRva = {hex(call_rva)};
inline constexpr std::uint32_t kCallTargetRva = {hex(target_rva)};
inline constexpr std::array<std::uint8_t, {len(window)}> kCallWindow = {{{', '.join(hex(b) for b in window)}}};
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
