#!/usr/bin/env python3
"""Validate the canonical RVA catalog and deterministically generate C++ artifacts."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "src/game/rva_catalog.json"

TOP_LEVEL_KEYS = {
    "schema_version", "catalog_id", "cataloged_on", "default_build", "builds", "addresses", "locators",
}
BUILD_KEYS = {
    "id", "game_version", "pe_timestamp", "size_of_image", "pe_checksum", "file_size", "sha256"
}
ADDRESS_KEYS = {
    "id", "cpp_symbol", "kind", "subsystem", "requirement", "validation",
    "install_policy", "status", "hook_owner", "consumers", "hook_spec", "builds",
}
ADDRESS_BUILD_KEYS = {"rva", "signature", "evidence"}
VALIDATION_KEYS = {"policy"}
SIGNATURE_KEYS = {"bytes", "mask"}
EVIDENCE_KEYS = {"type", "path", "detail"}
HOOK_SPEC_KEYS = {"name", "order", "required_for_release_startup"}
LOCATOR_KEYS = {"id", "cpp_symbol", "match_policy", "consumers", "builds"}
LOCATOR_BUILD_KEYS = {"pattern", "decode", "candidate_adjustments", "evidence"}
LOCATOR_PATTERN_KEYS = {"bytes", "mask"}
LOCATOR_DECODE_KEYS = {"kind", "displacement_offset", "instruction_size"}

ALLOWED_KINDS = {"function", "call_site", "global"}
ALLOWED_REQUIREMENTS = {"release", "optional", "research"}
ALLOWED_VALIDATION_POLICIES = {"signature", "build_identity", "call_site", "data_reference"}
ALLOWED_INSTALL_POLICIES = {"required_hook", "optional_hook", "signature_only", "callable", "data_only"}
ALLOWED_STATUSES = {"production", "diagnostic", "research"}
REQUIRED_SIGNATURE_IDS = {
    "set_string_text", "piano_score_parser", "piano_event_deep_copy",
    "piano_event_construct", "piano_event_link", "piano_event_destruct",
    "create_package", "static_construct_object", "bgm_slot_setup", "piano_audio_request",
}

class CatalogError(ValueError):
    pass


def validate_install_relationships(entry: dict, hook_spec: dict | None,
                                   signature: tuple[list[int], str] | None, context: str) -> None:
    install_policy = entry["install_policy"]
    hook_owner = entry["hook_owner"]
    if install_policy == "required_hook":
        if entry["requirement"] != "release" or hook_owner is None or hook_spec is None:
            raise CatalogError(f"{context} required_hook requires release status, an owner, and a hook_spec")
        if not hook_spec["required_for_release_startup"]:
            raise CatalogError(f"{context} required_hook must participate in release startup validation")
    elif install_policy == "optional_hook":
        if hook_owner is None or hook_spec is None:
            raise CatalogError(f"{context} optional_hook requires an owner and a hook_spec")
        if hook_spec["required_for_release_startup"]:
            raise CatalogError(f"{context} optional_hook cannot be release-startup-required")
    else:
        if hook_owner is not None:
            raise CatalogError(f"{context} non-hook install policy cannot declare a hook owner")
        if hook_spec is not None and hook_spec["required_for_release_startup"]:
            raise CatalogError(f"{context} non-hook startup validation cannot be required")
    if install_policy == "data_only" and (hook_spec is not None or signature is not None):
        raise CatalogError(f"{context} data_only entries cannot own hook or signature metadata")
    if install_policy == "signature_only" and signature is None:
        raise CatalogError(f"{context} signature_only entries require a signature")


def validate_policy_self_tests() -> None:
    signature = ([0x90], "x")
    required = {"install_policy": "required_hook", "requirement": "release", "hook_owner": "owner"}
    optional = {"install_policy": "optional_hook", "requirement": "optional", "hook_owner": "owner"}
    required_spec = {"required_for_release_startup": True}
    optional_spec = {"required_for_release_startup": False}
    validate_install_relationships(required, required_spec, signature, "self_test.valid_required")
    validate_install_relationships(optional, optional_spec, signature, "self_test.valid_optional")
    validate_install_relationships(
        {"install_policy": "callable", "requirement": "optional", "hook_owner": None},
        optional_spec, signature, "self_test.valid_optional_preflight")
    invalid_cases = (
        ({**required, "hook_owner": None}, required_spec, signature),
        (required, None, signature),
        (required, optional_spec, signature),
        (optional, None, signature),
        (optional, required_spec, signature),
        ({"install_policy": "data_only", "requirement": "release", "hook_owner": None}, optional_spec, signature),
        ({"install_policy": "signature_only", "requirement": "research", "hook_owner": None}, None, None),
    )
    for index, (entry, hook_spec, parsed_signature) in enumerate(invalid_cases):
        try:
            validate_install_relationships(entry, hook_spec, parsed_signature, f"self_test.invalid[{index}]")
        except CatalogError:
            continue
        raise CatalogError(f"internal policy self-test {index} accepted an invalid relationship")


def require_keys(value: object, expected: set[str], context: str) -> dict:
    if not isinstance(value, dict):
        raise CatalogError(f"{context} must be an object")
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise CatalogError(f"{context} has invalid keys; missing={missing} extra={extra}")
    return value


def parse_hex(value: object, context: str, bits: int = 64) -> int:
    if not isinstance(value, str) or not re.fullmatch(r"0x[0-9a-f]+", value):
        raise CatalogError(f"{context} must be a lowercase hexadecimal string")
    parsed = int(value, 16)
    if parsed < 0 or parsed >= 1 << bits:
        raise CatalogError(f"{context} is outside the uint{bits} range")
    return parsed


def parse_signature(value: object, context: str) -> tuple[list[int], str] | None:
    if value is None:
        return None
    signature = require_keys(value, SIGNATURE_KEYS, context)
    bytes_text = signature["bytes"]
    mask = signature["mask"]
    if not isinstance(bytes_text, str) or not re.fullmatch(r"[0-9a-f]{2}( [0-9a-f]{2})*", bytes_text):
        raise CatalogError(f"{context}.bytes must be lowercase space-separated bytes")
    if not isinstance(mask, str) or not re.fullmatch(r"x+", mask):
        raise CatalogError(f"{context}.mask must contain only 'x'; masked runtime matching is unsupported")
    values = [int(part, 16) for part in bytes_text.split()]
    if len(values) != len(mask):
        raise CatalogError(f"{context} byte and mask lengths differ")
    if not values:
        raise CatalogError(f"{context} must not be empty")
    return values, mask


def parse_locator_pattern(value: object, context: str) -> tuple[list[int], str]:
    pattern = require_keys(value, LOCATOR_PATTERN_KEYS, context)
    bytes_text = pattern["bytes"]
    mask = pattern["mask"]
    if not isinstance(bytes_text, str) or not re.fullmatch(r"[0-9a-f]{2}( [0-9a-f]{2})*", bytes_text):
        raise CatalogError(f"{context}.bytes must be lowercase space-separated fixed bytes")
    if not isinstance(mask, str) or not re.fullmatch(r"[x?]+", mask):
        raise CatalogError(f"{context}.mask must contain only 'x' and '?'")
    values = [int(part, 16) for part in bytes_text.split()]
    if len(values) != len(mask):
        raise CatalogError(f"{context} byte and mask lengths differ")
    if not values or "x" not in mask:
        raise CatalogError(f"{context} must contain at least one fixed byte")
    return values, mask


def validate_repository_paths(items: object, context: str, root: Path) -> list[str]:
    if not isinstance(items, list) or not items or any(
        not isinstance(item, str) or not item for item in items
    ) or len(items) != len(set(items)):
        raise CatalogError(f"{context} must be a unique non-empty string array")
    for item in items:
        item_path = Path(item)
        if item_path.is_absolute() or ".." in item_path.parts or not (root / item_path).is_file():
            raise CatalogError(f"{context} contains a missing or non-repository file: {item}")
    return items


def validate_evidence(items: object, context: str, root: Path) -> list[dict]:
    if not isinstance(items, list) or not items:
        raise CatalogError(f"{context} must be non-empty")
    validated: list[dict] = []
    for index, raw_evidence in enumerate(items):
        evidence_context = f"{context}[{index}]"
        evidence_item = require_keys(raw_evidence, EVIDENCE_KEYS, evidence_context)
        if any(not isinstance(evidence_item[field], str) or not evidence_item[field] for field in EVIDENCE_KEYS):
            raise CatalogError(f"{evidence_context} has an empty field")
        evidence_path = Path(evidence_item["path"])
        if evidence_path.is_absolute() or ".." in evidence_path.parts or not (root / evidence_path).is_file():
            raise CatalogError(f"{evidence_context} references a missing repository file")
        validated.append(evidence_item)
    return validated


def validate_locators(raw_locators: object, root: Path, build_ids: set[str]) -> list[dict]:
    if not isinstance(raw_locators, list) or not raw_locators:
        raise CatalogError("locators must be a non-empty array")
    ids: set[str] = set()
    symbols: set[str] = set()
    parsed: list[dict] = []
    for index, raw_locator in enumerate(raw_locators):
        context = f"locators[{index}]"
        locator = require_keys(raw_locator, LOCATOR_KEYS, context)
        identifier = locator["id"]
        symbol = locator["cpp_symbol"]
        if not isinstance(identifier, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", identifier):
            raise CatalogError(f"{context}.id is invalid")
        if not isinstance(symbol, str) or not re.fullmatch(r"[A-Z][A-Za-z0-9]*", symbol):
            raise CatalogError(f"{context}.cpp_symbol is invalid")
        if identifier in ids or symbol in symbols:
            raise CatalogError(f"{context} duplicates an id or C++ symbol")
        ids.add(identifier)
        symbols.add(symbol)

        if locator["match_policy"] != "first":
            raise CatalogError(f"{context}.match_policy must be 'first'")
        validate_repository_paths(locator["consumers"], f"{context}.consumers", root)

        raw_builds_for_locator = locator["builds"]
        if not isinstance(raw_builds_for_locator, dict):
            raise CatalogError(f"{context}.builds must be an object keyed by build id")
        # A locator carries no requirement field and has no null-spec degradation path: a build
        # that cannot resolve it loads without runtime object identity and silently publishes
        # nothing. Absence is therefore never meaningful, and every declared build must name
        # exactly one derivation of its own.
        declared = set(raw_builds_for_locator)
        if declared != build_ids:
            raise CatalogError(
                f"{context}.builds must be derived for every declared build; "
                f"missing={sorted(build_ids - declared)} extra={sorted(declared - build_ids)}")

        locator_builds: dict[str, dict] = {}
        for build_id, raw_build_locator in raw_builds_for_locator.items():
            build_context = f"{context}.builds.{build_id}"
            build_locator = require_keys(raw_build_locator, LOCATOR_BUILD_KEYS, build_context)
            pattern = parse_locator_pattern(build_locator["pattern"], f"{build_context}.pattern")
            decode = require_keys(
                build_locator["decode"], LOCATOR_DECODE_KEYS, f"{build_context}.decode")
            if decode["kind"] != "rel32":
                raise CatalogError(f"{build_context}.decode.kind must be 'rel32'")
            displacement_offset = parse_hex(
                decode["displacement_offset"], f"{build_context}.decode.displacement_offset")
            instruction_size = parse_hex(
                decode["instruction_size"], f"{build_context}.decode.instruction_size")
            if displacement_offset + 4 > len(pattern[0]):
                raise CatalogError(
                    f"{build_context}.decode rel32 displacement is outside the pattern")
            if instruction_size == 0 or instruction_size > len(pattern[0]):
                raise CatalogError(f"{build_context}.decode.instruction_size is outside the pattern")
            if displacement_offset + 4 > instruction_size:
                raise CatalogError(
                    f"{build_context}.decode rel32 displacement is outside the instruction")

            raw_adjustments = build_locator["candidate_adjustments"]
            if not isinstance(raw_adjustments, list) or not raw_adjustments:
                raise CatalogError(f"{build_context}.candidate_adjustments must be a non-empty array")
            adjustments = [
                parse_hex(value, f"{build_context}.candidate_adjustments[{adjustment_index}]")
                for adjustment_index, value in enumerate(raw_adjustments)
            ]
            if len(adjustments) != len(set(adjustments)):
                raise CatalogError(f"{build_context}.candidate_adjustments must be unique")
            if adjustments[0] != 0:
                raise CatalogError(f"{build_context}.candidate_adjustments must begin with zero")

            locator_builds[build_id] = {
                "pattern": build_locator["pattern"],
                "pattern_value": pattern,
                "decode": decode,
                "displacement_offset_value": displacement_offset,
                "instruction_size_value": instruction_size,
                "candidate_adjustment_values": adjustments,
                "evidence": validate_evidence(
                    build_locator["evidence"], f"{build_context}.evidence", root),
            }
        parsed.append({**locator, "builds": locator_builds})
    return parsed


def validate_builds(raw_builds: object) -> tuple[list[dict], dict[str, int]]:
    if not isinstance(raw_builds, list) or not raw_builds:
        raise CatalogError("builds must be a non-empty array")
    ids: set[str] = set()
    parsed: list[dict] = []
    image_size_by_build: dict[str, int] = {}
    for index, raw_build in enumerate(raw_builds):
        context = f"builds[{index}]"
        build = require_keys(raw_build, BUILD_KEYS, context)
        identifier = build["id"]
        if not isinstance(identifier, str) or not re.fullmatch(r"ff7rebirth-[a-z0-9-]+", identifier):
            raise CatalogError(f"{context}.id is invalid")
        if identifier in ids:
            raise CatalogError(f"{context}.id duplicates an existing build id")
        if not isinstance(build["game_version"], str) or not re.fullmatch(r"[0-9]+\.[0-9]+", build["game_version"]):
            raise CatalogError(f"{context}.game_version is invalid")
        timestamp = parse_hex(build["pe_timestamp"], f"{context}.pe_timestamp", 32)
        image_size = parse_hex(build["size_of_image"], f"{context}.size_of_image", 32)
        checksum = parse_hex(build["pe_checksum"], f"{context}.pe_checksum", 32)
        if timestamp == 0 or image_size < 0x100000 or checksum == 0:
            raise CatalogError(f"{context} PE identity fields are not plausible")
        if not isinstance(build["file_size"], int) or isinstance(build["file_size"], bool) or build["file_size"] <= 0:
            raise CatalogError(f"{context}.file_size must be a positive integer")
        if not isinstance(build["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", build["sha256"]):
            raise CatalogError(f"{context}.sha256 must be a lowercase SHA-256 digest")
        ids.add(identifier)
        image_size_by_build[identifier] = image_size
        parsed.append(build)
    return parsed, image_size_by_build


def load_and_validate_catalog() -> tuple[dict, list[dict], list[dict]]:
    try:
        catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CatalogError(f"cannot read {CATALOG_PATH}: {error}") from error
    require_keys(catalog, TOP_LEVEL_KEYS, "catalog")
    if catalog["schema_version"] != 3:
        raise CatalogError("schema_version must be 3")
    if catalog["catalog_id"] != "ff7rpianosongs-rva-catalog":
        raise CatalogError("catalog_id is not canonical")
    if not isinstance(catalog["cataloged_on"], str) or not re.fullmatch(
        r"[0-9]{4}-[0-9]{2}-[0-9]{2}", catalog["cataloged_on"]
    ):
        raise CatalogError("cataloged_on must be an ISO date")

    builds, image_size_by_build = validate_builds(catalog["builds"])
    build_ids = set(image_size_by_build)
    if not isinstance(catalog["default_build"], str) or catalog["default_build"] not in build_ids:
        raise CatalogError("default_build must reference a declared build id")

    addresses = catalog["addresses"]
    if not isinstance(addresses, list) or not addresses:
        raise CatalogError("addresses must be a non-empty array")
    ids: set[str] = set()
    symbols: set[str] = set()
    rvas_by_build: dict[str, set[int]] = {build_id: set() for build_id in build_ids}
    hook_names: set[str] = set()
    hook_orders: list[int] = []
    parsed: list[dict] = []
    for index, raw_entry in enumerate(addresses):
        context = f"addresses[{index}]"
        entry = require_keys(raw_entry, ADDRESS_KEYS, context)
        identifier = entry["id"]
        symbol = entry["cpp_symbol"]
        if not isinstance(identifier, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", identifier):
            raise CatalogError(f"{context}.id is invalid")
        if not isinstance(symbol, str) or not re.fullmatch(r"[A-Z][A-Za-z0-9]*", symbol):
            raise CatalogError(f"{context}.cpp_symbol is invalid")
        if identifier in ids or symbol in symbols:
            raise CatalogError(f"{context} duplicates an id or C++ symbol")
        ids.add(identifier)
        symbols.add(symbol)
        for field, allowed in (
            ("kind", ALLOWED_KINDS), ("requirement", ALLOWED_REQUIREMENTS),
            ("install_policy", ALLOWED_INSTALL_POLICIES), ("status", ALLOWED_STATUSES),
        ):
            if entry[field] not in allowed:
                raise CatalogError(f"{context}.{field} is invalid")
        if not isinstance(entry["subsystem"], str) or not re.fullmatch(r"[a-z][a-z0-9_]*", entry["subsystem"]):
            raise CatalogError(f"{context}.subsystem is invalid")
        if entry["hook_owner"] is not None and (
            not isinstance(entry["hook_owner"], str)
            or not re.fullmatch(r"[a-z][a-z0-9_]*", entry["hook_owner"])
        ):
            raise CatalogError(f"{context}.hook_owner is invalid")
        validate_repository_paths(entry["consumers"], f"{context}.consumers", ROOT)

        validation = require_keys(entry["validation"], VALIDATION_KEYS, f"{context}.validation")
        if validation["policy"] not in ALLOWED_VALIDATION_POLICIES:
            raise CatalogError(f"{context}.validation.policy is invalid")

        hook_spec = entry["hook_spec"]
        if hook_spec is not None:
            hook = require_keys(hook_spec, HOOK_SPEC_KEYS, f"{context}.hook_spec")
            if not isinstance(hook["name"], str) or not re.fullmatch(r"[a-z][a-z0-9_]*", hook["name"]):
                raise CatalogError(f"{context}.hook_spec.name is invalid")
            if hook["name"] in hook_names:
                raise CatalogError(f"{context}.hook_spec.name is duplicated")
            if not isinstance(hook["order"], int) or isinstance(hook["order"], bool) or hook["order"] < 0:
                raise CatalogError(f"{context}.hook_spec.order is invalid")
            if not isinstance(hook["required_for_release_startup"], bool):
                raise CatalogError(f"{context}.hook_spec.required_for_release_startup must be boolean")
            hook_names.add(hook["name"])
            hook_orders.append(hook["order"])

        raw_builds_for_entry = entry["builds"]
        if not isinstance(raw_builds_for_entry, dict) or not raw_builds_for_entry:
            raise CatalogError(f"{context}.builds must be a non-empty object naming at least one declared build")
        if entry["requirement"] == "release" and set(raw_builds_for_entry) != build_ids:
            raise CatalogError(f"{context} is release and must have a value in every declared build")

        entry_builds: dict[str, dict] = {}
        for build_id, raw_build_entry in raw_builds_for_entry.items():
            build_context = f"{context}.builds.{build_id}"
            if build_id not in build_ids:
                raise CatalogError(f"{build_context} references an undeclared build id")
            build_entry = require_keys(raw_build_entry, ADDRESS_BUILD_KEYS, build_context)
            rva = parse_hex(build_entry["rva"], f"{build_context}.rva")
            if rva == 0 or rva >= image_size_by_build[build_id]:
                raise CatalogError(f"{build_context}.rva is outside the executable image")
            if rva in rvas_by_build[build_id]:
                raise CatalogError(f"{build_context} duplicates an RVA already used in build {build_id}")
            rvas_by_build[build_id].add(rva)

            signature = parse_signature(build_entry["signature"], f"{build_context}.signature")
            if validation["policy"] == "signature" and signature is None:
                raise CatalogError(f"{build_context} requires a signature")
            if identifier in REQUIRED_SIGNATURE_IDS and signature is None:
                raise CatalogError(f"{build_context} is missing its required centralized signature")
            if hook_spec is not None and signature is None:
                raise CatalogError(f"{build_context}.hook_spec requires a signature")

            evidence = validate_evidence(build_entry["evidence"], f"{build_context}.evidence", ROOT)
            validate_install_relationships(entry, hook_spec, signature, build_context)

            entry_builds[build_id] = {
                "rva": build_entry["rva"],
                "rva_value": rva,
                "signature": build_entry["signature"],
                "signature_value": signature,
                "evidence": evidence,
            }

        parsed.append({**entry, "builds": entry_builds})

    if not parsed or not hook_orders:
        raise CatalogError("addresses and hook specifications must not be empty")
    if sorted(hook_orders) != list(range(len(hook_orders))):
        raise CatalogError("hook_spec.order values must be contiguous from zero")
    validate_consumer_references(parsed)
    validate_source_literals(parsed, max(image_size_by_build.values()))
    locators = validate_locators(catalog["locators"], ROOT, build_ids)
    return catalog, parsed, locators


def validate_consumer_references(entries: list[dict]) -> None:
    by_symbol = {entry["cpp_symbol"]: entry for entry in entries}
    failures: list[str] = []
    for entry in entries:
        symbol_token = f"rva::{entry['cpp_symbol']}"
        id_token = f'"{entry["id"]}"'
        for consumer in entry["consumers"]:
            text = (ROOT / consumer).read_text(encoding="utf-8", errors="strict")
            if symbol_token not in text and id_token not in text:
                failures.append(f"{entry['id']}: stale consumer without a symbol or id reference: {consumer}")

    for path in sorted((ROOT / "src/game").rglob("*")):
        if path.suffix not in {".cpp", ".h"} or "generated" in path.parts:
            continue
        relative = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8", errors="strict")
        for match in re.finditer(r"\brva::([A-Z][A-Za-z0-9]*)\b", text):
            entry = by_symbol.get(match.group(1))
            if entry is None:
                failures.append(f"{relative}: references unknown generated RVA symbol {match.group(1)}")
            elif relative not in entry["consumers"]:
                failures.append(f"{entry['id']}: missing consumer metadata for {relative}")
    if failures:
        raise CatalogError("catalog consumer validation failed:\n" + "\n".join(sorted(set(failures))))


def validate_source_literals(entries: list[dict], image_size: int) -> None:
    by_rva: dict[int, str] = {}
    for entry in entries:
        for build_data in entry["builds"].values():
            by_rva[build_data["rva_value"]] = entry["id"]
    literal_pattern = re.compile(r"0x[0-9a-fA-F]+(?:[uUlL]*)")
    ownership_pattern = re.compile(r"(?:rva|address|caller|exe_base|exe_module|module_base)", re.IGNORECASE)
    failures: list[str] = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in {".cpp", ".h"} or "tests" in path.parts or "generated" in path.parts:
            continue
        relative = path.relative_to(ROOT).as_posix()
        for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="strict").splitlines(), 1):
            for match in literal_pattern.finditer(line):
                literal = match.group(0)
                value = int(re.sub(r"[uUlL]+$", "", literal), 16)
                catalog_id = by_rva.get(value)
                if catalog_id is not None:
                    failures.append(f"{relative}:{line_number}: catalog RVA {catalog_id} is owned outside generated code")
                elif 0x100000 <= value < image_size and ownership_pattern.search(line):
                    failures.append(f"{relative}:{line_number}: uncataloged executable-RVA-shaped literal {match.group(0)}")
    if failures:
        raise CatalogError("production RVA literal scan failed:\n" + "\n".join(failures))


def format_bytes(values: list[int]) -> str:
    return ", ".join(f"0x{value:02x}" for value in values)


def format_uintptrs(values: list[int]) -> str:
    return ", ".join(f"0x{value:02x}" for value in values)


def render_rvas(entries: list[dict], build_id: str) -> bytes:
    lines = [
        "// Generated by tools/generate_rva_catalog.py from src/game/rva_catalog.json. Do not edit.",
        "#pragma once", "", "#include <cstdint>", "",
        "namespace ff7r::piano::game::rva {", "",
    ]
    for entry in entries:
        build_data = entry["builds"].get(build_id)
        if build_data is None:
            lines.append(
                f"inline constexpr uintptr_t {entry['cpp_symbol']} = 0x0; // absent in build {build_id}"
            )
        else:
            lines.append(f"inline constexpr uintptr_t {entry['cpp_symbol']} = {build_data['rva']};")
    lines.extend(["", "} // namespace ff7r::piano::game::rva", ""])
    return "\n".join(lines).encode("ascii")


def render_hook_specs(entries: list[dict], build_id: str) -> bytes:
    hooks = sorted(
        (entry for entry in entries if entry["hook_spec"] and build_id in entry["builds"]),
        key=lambda item: item["hook_spec"]["order"],
    )
    lines = ["// Generated by tools/generate_rva_catalog.py from src/game/rva_catalog.json. Do not edit."]
    for entry in hooks:
        hook = entry["hook_spec"]
        values, _ = entry["builds"][build_id]["signature_value"]
        required = "true" if hook["required_for_release_startup"] else "false"
        lines.append(
            f"{{\"{hook['name']}\", rva::{entry['cpp_symbol']}, {{{format_bytes(values)}}}, {required}}},"
        )
    lines.append("")
    return "\n".join(lines).encode("ascii")


def render_rva_signatures(entries: list[dict], build_id: str) -> bytes:
    lines = ["// Generated by tools/generate_rva_catalog.py from src/game/rva_catalog.json. Do not edit."]
    for entry in entries:
        build_data = entry["builds"].get(build_id)
        if build_data is None or build_data["signature_value"] is None:
            continue
        values, _ = build_data["signature_value"]
        lines.append(
            f"{{\"{entry['id']}\", rva::{entry['cpp_symbol']}, {{{format_bytes(values)}}}}},"
        )
    lines.append("")
    return "\n".join(lines).encode("ascii")


def render_build_identity(build: dict) -> bytes:
    lines = [
        "// Generated by tools/generate_rva_catalog.py from src/game/rva_catalog.json. Do not edit.",
        "#pragma once", "", "#include <cstddef>", "#include <cstdint>", "#include <string_view>", "",
        "namespace ff7r::piano::core::generated {", "",
        f"inline constexpr std::string_view kBuildId = \"{build['id']}\";",
        f"inline constexpr uint32_t kExeTimestamp = {build['pe_timestamp']};",
        f"inline constexpr uint32_t kSizeOfImage = {build['size_of_image']};",
        f"inline constexpr uint32_t kPeChecksum = {build['pe_checksum']};",
        f"inline constexpr std::size_t kExeFileSize = {build['file_size']};",
        f"inline constexpr std::string_view kExeSha256 = \"{build['sha256']}\";",
        "", "} // namespace ff7r::piano::core::generated", "",
    ]
    return "\n".join(lines).encode("ascii")


def render_runtime_locator_specs(locators: list[dict], build_id: str) -> bytes:
    lines = ["// Generated by tools/generate_rva_catalog.py from src/game/rva_catalog.json. Do not edit.", ""]
    for locator in locators:
        symbol = locator["cpp_symbol"]
        build_data = locator["builds"][build_id]
        values, mask = build_data["pattern_value"]
        adjustments = build_data["candidate_adjustment_values"]
        lines.extend([
            f"constexpr std::array<std::uint8_t, {len(values)}> k{symbol}PatternBytes{{{{{format_bytes(values)}}}}};",
            f"constexpr std::array<std::uintptr_t, {len(adjustments)}> k{symbol}CandidateAdjustments{{{{{format_uintptrs(adjustments)}}}}};",
            "",
        ])
    lines.append(f"constexpr std::array<RuntimeLocatorSpec, {len(locators)}> kRuntimeLocatorSpecs{{{{")
    for locator in locators:
        symbol = locator["cpp_symbol"]
        build_data = locator["builds"][build_id]
        _, mask = build_data["pattern_value"]
        lines.append(
            f'    {{"{locator["id"]}", '
            f'{{k{symbol}PatternBytes.data(), "{mask}", k{symbol}PatternBytes.size()}}, '
            "RuntimeLocatorMatchPolicy::First, "
            f'{{RuntimeLocatorDecodeKind::Rel32, {build_data["displacement_offset_value"]}, '
            f'{build_data["instruction_size_value"]}}}, '
            f'{{k{symbol}CandidateAdjustments.data(), k{symbol}CandidateAdjustments.size()}}}},'
        )
    lines.extend(["}};", ""])
    return "\n".join(lines).encode("ascii")


def build_output_paths(build_id: str) -> tuple[Path, ...]:
    base = Path("src/generated") / build_id
    return (
        base / "game/generated/rvas.generated.h",
        base / "game/generated/hook_specs.generated.inc",
        base / "game/generated/rva_signatures.generated.inc",
        base / "core/generated/build_identity.generated.h",
        base / "game/generated/runtime_locator_specs.generated.inc",
    )


def generated_outputs(catalog: dict, entries: list[dict], locators: list[dict]) -> dict[Path, bytes]:
    # Raw source hashes belong to build provenance, not compilation-facing identity.
    outputs: dict[Path, bytes] = {}
    for build in catalog["builds"]:
        build_id = build["id"]
        paths = build_output_paths(build_id)
        outputs[paths[0]] = render_rvas(entries, build_id)
        outputs[paths[1]] = render_hook_specs(entries, build_id)
        outputs[paths[2]] = render_rva_signatures(entries, build_id)
        outputs[paths[3]] = render_build_identity(build)
        outputs[paths[4]] = render_runtime_locator_specs(locators, build_id)
    return outputs


def write_outputs(root: Path, outputs: dict[Path, bytes]) -> None:
    for relative, content in outputs.items():
        destination = root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(content)


def check_outputs(outputs: dict[Path, bytes], temp_root: Path | None) -> None:
    if temp_root is None:
        with tempfile.TemporaryDirectory(prefix="ff7rp-rva-catalog-") as directory:
            compare_outputs(outputs, Path(directory))
        return
    if temp_root.exists():
        shutil.rmtree(temp_root)
    temp_root.mkdir(parents=True)
    compare_outputs(outputs, temp_root)


def compare_outputs(outputs: dict[Path, bytes], temp_root: Path) -> None:
    write_outputs(temp_root, outputs)
    failures = []
    for relative in outputs:
        tracked = ROOT / relative
        generated = temp_root / relative
        if not tracked.is_file():
            failures.append(f"missing tracked generated file: {relative.as_posix()}")
        elif tracked.read_bytes() != generated.read_bytes():
            failures.append(f"stale tracked generated file: {relative.as_posix()}")
    if failures:
        raise CatalogError("generated artifact byte comparison failed:\n" + "\n".join(failures))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="write tracked generated artifacts for every build")
    mode.add_argument("--check", action="store_true", help="validate and byte-compare tracked artifacts for every build")
    parser.add_argument("--temp-dir", type=Path, help="deterministic comparison directory for --check")
    args = parser.parse_args()
    if args.temp_dir and not args.check:
        parser.error("--temp-dir requires --check")
    try:
        validate_policy_self_tests()
        catalog, entries, locators = load_and_validate_catalog()
        outputs = generated_outputs(catalog, entries, locators)
        if args.write:
            write_outputs(ROOT, outputs)
            action = "wrote"
        else:
            check_outputs(outputs, args.temp_dir)
            action = "checked"
        hook_count = sum(1 for entry in entries if entry["hook_spec"])
        required_count = sum(
            1 for entry in entries
            if entry["hook_spec"] and entry["hook_spec"]["required_for_release_startup"]
        )
        build_ids = ",".join(build["id"] for build in catalog["builds"])
        print(
            f"rva catalog {action}: schema=3 addresses={len(entries)} locators={len(locators)} hooks={hook_count} "
            f"required={required_count} optional={hook_count - required_count} builds={build_ids}"
        )
        return 0
    except CatalogError as error:
        print(f"rva catalog error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
