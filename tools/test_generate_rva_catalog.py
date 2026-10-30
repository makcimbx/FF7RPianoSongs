from __future__ import annotations

import copy
import importlib.util
import json
import re
import tempfile
import unittest
from typing import Any
from pathlib import Path
from unittest import mock


GENERATOR_PATH = Path(__file__).with_name("generate_rva_catalog.py")
SPEC = importlib.util.spec_from_file_location("generate_rva_catalog", GENERATOR_PATH)
assert SPEC is not None and SPEC.loader is not None
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


class RuntimeLocatorGeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.catalog, cls.entries, cls.locators = generator.load_and_validate_catalog()
        cls.build_ids = {build["id"] for build in cls.catalog["builds"]}
        cls.default_build = cls.catalog["default_build"]

    def locator(self) -> dict:
        return copy.deepcopy(self.catalog["locators"][0])

    def build_locator(self, locator: dict) -> dict:
        return locator["builds"][self.default_build]

    def pattern_length(self, locator: dict) -> int:
        return len(self.build_locator(locator)["pattern"]["mask"])

    def validate(self, locator: dict) -> list[dict]:
        return generator.validate_locators([locator], generator.ROOT, self.build_ids)

    def test_locator_schema_is_strict(self) -> None:
        locator = self.locator()
        locator["unexpected"] = True
        with self.assertRaisesRegex(generator.CatalogError, "invalid keys"):
            self.validate(locator)

    def test_locator_build_schema_is_strict(self) -> None:
        locator = self.locator()
        self.build_locator(locator)["unexpected"] = True
        with self.assertRaisesRegex(generator.CatalogError, "invalid keys"):
            self.validate(locator)

    def test_hook_signatures_remain_fixed_only(self) -> None:
        with self.assertRaisesRegex(generator.CatalogError, "mask must contain only 'x'"):
            generator.parse_signature(
                {"bytes": "90 00", "mask": "x?"},
                "addresses[0].validation.signature",
            )

    def test_locator_masks_are_strict_and_match_byte_length(self) -> None:
        invalid_character = self.locator()
        invalid_pattern = self.build_locator(invalid_character)["pattern"]
        invalid_pattern["mask"] = "." * len(invalid_pattern["mask"])
        with self.assertRaisesRegex(generator.CatalogError, "only 'x' and '\\?'"):
            self.validate(invalid_character)

        wrong_length = self.locator()
        short_pattern = self.build_locator(wrong_length)["pattern"]
        short_pattern["mask"] = short_pattern["mask"][:-1]
        with self.assertRaisesRegex(generator.CatalogError, "lengths differ"):
            self.validate(wrong_length)

    def test_rel32_and_instruction_offsets_must_fit_pattern(self) -> None:
        rel32_out_of_bounds = self.locator()
        self.build_locator(rel32_out_of_bounds)["decode"]["displacement_offset"] = (
            f"0x{self.pattern_length(rel32_out_of_bounds) - 3:02x}")
        with self.assertRaisesRegex(generator.CatalogError, "rel32 displacement is outside the pattern"):
            self.validate(rel32_out_of_bounds)

        instruction_out_of_bounds = self.locator()
        self.build_locator(instruction_out_of_bounds)["decode"]["instruction_size"] = (
            f"0x{self.pattern_length(instruction_out_of_bounds) + 1:02x}")
        with self.assertRaisesRegex(generator.CatalogError, "instruction_size is outside"):
            self.validate(instruction_out_of_bounds)

    def test_rel32_displacement_must_fit_instruction(self) -> None:
        displacement_outside_instruction = self.locator()
        self.build_locator(displacement_outside_instruction)["decode"]["instruction_size"] = "0x05"
        with self.assertRaisesRegex(generator.CatalogError, "rel32 displacement is outside the instruction"):
            self.validate(displacement_outside_instruction)

    def test_adjustments_reject_duplicates_and_require_zero_first(self) -> None:
        duplicate = self.locator()
        self.build_locator(duplicate)["candidate_adjustments"] = ["0x00", "0x24", "0x24"]
        with self.assertRaisesRegex(generator.CatalogError, "must be unique"):
            self.validate(duplicate)

        reordered_zero = self.locator()
        self.build_locator(reordered_zero)["candidate_adjustments"] = ["0x24", "0x00", "0x20"]
        with self.assertRaisesRegex(generator.CatalogError, "begin with zero"):
            self.validate(reordered_zero)

    def test_locator_must_be_derived_for_every_declared_build(self) -> None:
        missing_build = self.locator()
        missing_build["builds"].pop(self.default_build)
        with self.assertRaisesRegex(generator.CatalogError, "must be derived for every declared build"):
            self.validate(missing_build)

        undeclared_build = self.locator()
        undeclared_build["builds"]["ff7rebirth-steam-win64-33333333"] = copy.deepcopy(
            undeclared_build["builds"][self.default_build])
        with self.assertRaisesRegex(generator.CatalogError, "must be derived for every declared build"):
            self.validate(undeclared_build)

    def test_nonzero_adjustment_order_is_preserved_without_sorting(self) -> None:
        locator = self.locator()
        self.build_locator(locator)["candidate_adjustments"] = ["0x00", "0x10", "0x30", "0x24", "0x20"]
        parsed = self.validate(locator)
        rendered = generator.render_runtime_locator_specs(parsed, self.default_build).decode("ascii")
        self.assertIn("{{0x00, 0x10, 0x30, 0x24, 0x20}}", rendered)

    def test_generated_api_contains_each_builds_own_catalog_data(self) -> None:
        for build_id in self.build_ids:
            rendered = generator.render_runtime_locator_specs(self.locators, build_id).decode("ascii")
            self.assertIn("RuntimeLocatorMatchPolicy::First", rendered)
            for locator in self.locators:
                build_data = locator["builds"][build_id]
                _, mask = build_data["pattern_value"]
                self.assertIn(f"k{locator['cpp_symbol']}PatternBytes", rendered)
                self.assertIn(f'"{mask}"', rendered)
                self.assertIn(
                    f"RuntimeLocatorDecodeKind::Rel32, {build_data['displacement_offset_value']}, "
                    f"{build_data['instruction_size_value']}",
                    rendered,
                )
                self.assertIn(
                    "{{" + generator.format_uintptrs(build_data["candidate_adjustment_values"]) + "}}",
                    rendered,
                )

    def test_builds_with_different_patterns_do_not_share_rendered_specs(self) -> None:
        """A locator re-derived for one build must not leak another build's pattern."""
        rendered = {
            build_id: generator.render_runtime_locator_specs(self.locators, build_id)
            for build_id in self.build_ids
        }
        for locator in self.locators:
            for build_id, other_id in ((a, b) for a in self.build_ids for b in self.build_ids if a < b):
                if locator["builds"][build_id]["pattern"] == locator["builds"][other_id]["pattern"]:
                    continue
                self.assertNotEqual(
                    rendered[build_id], rendered[other_id],
                    f"{locator['id']} renders identically for {build_id} and {other_id}")

    def test_write_and_check_are_deterministic_and_fail_on_stale_output(self) -> None:
        outputs = generator.generated_outputs(self.catalog, self.entries, self.locators)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(generator, "ROOT", root):
                generator.write_outputs(root, outputs)
                first = {path: (root / path).read_bytes() for path in outputs}
                generator.check_outputs(outputs, root / "comparison")
                generator.write_outputs(root, outputs)
                second = {path: (root / path).read_bytes() for path in outputs}
                self.assertEqual(first, second)

                stale_path = root / next(iter(outputs))
                stale_path.write_bytes(b"stale\n")
                with self.assertRaisesRegex(generator.CatalogError, "stale tracked generated file"):
                    generator.check_outputs(outputs, root / "comparison")

    def test_source_formatting_does_not_change_generated_outputs(self) -> None:
        expected = generator.generated_outputs(self.catalog, self.entries, self.locators)
        # JSON has no comments. Exercise whitespace/key order in the catalog and
        # comment/blank-line changes in the generator entirely in memory.
        formatted_catalog = json.dumps(self.catalog, indent=4, sort_keys=True)
        original_read_text = Path.read_text

        def read_text(path, *args, **kwargs):
            if path == generator.CATALOG_PATH:
                return formatted_catalog
            return original_read_text(path, *args, **kwargs)

        with mock.patch.object(Path, "read_text", read_text):
            catalog, entries, locators = generator.load_and_validate_catalog()
        self.assertEqual(expected, generator.generated_outputs(catalog, entries, locators))
        source = GENERATOR_PATH.read_text(encoding="utf-8")
        namespace: dict[str, Any] = {"__file__": str(GENERATOR_PATH), "__name__": "formatting_test"}
        exec(compile(source + "\n\n# Comment-only generator edit.\n", str(GENERATOR_PATH), "exec"), namespace)
        # Rendering must not reread raw inputs, even to stamp a digest in a header.
        with mock.patch.object(Path, "read_bytes", side_effect=AssertionError("raw source read during rendering")):
            self.assertEqual(expected, namespace["generated_outputs"](catalog, entries, locators))


class MultiBuildCatalogTests(unittest.TestCase):
    """Multi-build coverage, complete runtime profiles and absence handling."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.raw_catalog = json.loads(generator.CATALOG_PATH.read_text(encoding="utf-8"))

    def raw(self) -> dict:
        return copy.deepcopy(self.raw_catalog)

    def load(self, catalog: dict) -> tuple[dict, list[dict], list[dict]]:
        with tempfile.TemporaryDirectory() as directory:
            temp_path = Path(directory) / "rva_catalog.json"
            temp_path.write_text(json.dumps(catalog), encoding="utf-8")
            with mock.patch.object(generator, "CATALOG_PATH", temp_path):
                return generator.load_and_validate_catalog()

    def add_second_build(self, catalog: dict, new_build_id: str, omit_ids: set[str] = frozenset()) -> dict:
        source_build = catalog["builds"][0]
        catalog["builds"].append({**source_build, "id": new_build_id})
        source_build_id = source_build["id"]
        for entry in catalog["addresses"]:
            if entry["id"] in omit_ids or source_build_id not in entry["builds"]:
                continue
            entry["builds"][new_build_id] = copy.deepcopy(entry["builds"][source_build_id])
        # Locators admit no absence, so a new build must always carry its own derivation.
        for locator in catalog["locators"]:
            locator["builds"][new_build_id] = copy.deepcopy(locator["builds"][source_build_id])
        return catalog

    def test_runtime_profile_is_required_for_every_declared_build(self) -> None:
        for index in range(len(self.raw_catalog["builds"])):
            for field in (None, "native_audio", "extended_chart", "evidence"):
                with self.subTest(build=index, field=field):
                    catalog = self.raw()
                    build = catalog["builds"][index]
                    if field is None:
                        build.pop("runtime_profile")
                    else:
                        build["runtime_profile"].pop(field)
                    with self.assertRaisesRegex(generator.CatalogError, "invalid keys"):
                        self.load(catalog)

    def test_runtime_profile_rejects_invalid_layouts_and_byte_contracts(self) -> None:
        cases = [
            ("native_audio", "bgm_resource", "0x0", "pointer-aligned"),
            ("native_audio", "append_slot", "0x281", "pointer-aligned"),
            ("extended_chart", "persistent_expand_call", "90 00 00 00 00", "CALL rel32"),
            ("extended_chart", "persistent_expand_call", "e8 00", "5 fixed bytes"),
            ("extended_chart", "fname_signature", "90", "32 fixed bytes"),
            ("extended_chart", "fname_signature", None, "32 fixed bytes"),
            ("extended_chart", "callback_vtable_slots", ["0x1"], "four RVAs"),
            ("extended_chart", "callback_vtable_slots", ["0x0", "0x1", "0x2", "0x3"], "in-image"),
            ("extended_chart", "callback_vtable_slots", ["0xffffffff", "0x1", "0x2", "0x3"], "in-image"),
            ("extended_chart", "callback_vtable_slots", ["0x1", "0x1", "0x2", "0x3"], "distinct"),
        ]
        for section, field, value, error in cases:
            with self.subTest(section=section, field=field, value=value):
                catalog = self.raw()
                catalog["builds"][0]["runtime_profile"][section][field] = value
                with self.assertRaisesRegex(generator.CatalogError, error):
                    self.load(catalog)
        catalog = self.raw()
        audio = catalog["builds"][0]["runtime_profile"]["native_audio"]
        audio["append_slot"] = "0x0" + audio["backing_slot"][2:]
        with self.assertRaisesRegex(generator.CatalogError, "virtual slots must be distinct"):
            self.load(catalog)

    def test_runtime_profile_requires_retained_evidence(self) -> None:
        catalog = self.raw()
        catalog["builds"][0]["runtime_profile"]["evidence"] = []
        with self.assertRaisesRegex(generator.CatalogError, "evidence must be non-empty"):
            self.load(catalog)

    def test_runtime_profile_generation_preserves_build_specific_values(self) -> None:
        catalog = self.raw()
        rendered = generator.render_runtime_build_profiles(catalog["builds"]).decode("ascii")
        for build in catalog["builds"]:
            profile = build["runtime_profile"]
            audio = ", ".join(profile["native_audio"][key] for key in generator.NATIVE_AUDIO_LAYOUT_FIELDS)
            self.assertIn(f'{{"{build["id"]}", {audio}}}', rendered)
            for field in ("persistent_expand_call", "fname_signature"):
                expected = generator.format_bytes(bytes.fromhex(profile["extended_chart"][field]))
                self.assertIn("{" + expected + "}", rendered)
            self.assertIn("{" + ", ".join(profile["extended_chart"]["callback_vtable_slots"]) + "}", rendered)

    def test_runtime_profile_edit_does_not_change_other_generated_contracts(self) -> None:
        catalog, entries, locators = self.load(self.raw())
        before = generator.generated_outputs(catalog, entries, locators)
        catalog["builds"][0]["runtime_profile"]["native_audio"]["bgm_resource"] = "0x68"
        after = generator.generated_outputs(catalog, entries, locators)
        for path, original in before.items():
            if path.name == "runtime_build_profiles.generated.inc":
                self.assertNotEqual(original, after[path])
            else:
                self.assertEqual(original, after[path], path)

    def test_new_build_preserves_source_absence(self) -> None:
        catalog = self.raw()
        source_build_id = catalog["builds"][0]["id"]
        source_absent = next(entry for entry in catalog["addresses"]
                             if source_build_id not in entry["builds"])
        new_build_id = "ff7rebirth-steam-win64-55555555"
        self.add_second_build(catalog, new_build_id)
        self.assertNotIn(new_build_id, source_absent["builds"])
        self.load(catalog)

    def test_release_entry_missing_from_a_build_is_rejected(self) -> None:
        catalog = self.raw()
        release_entry = next(entry for entry in catalog["addresses"] if entry["requirement"] == "release")
        self.add_second_build(catalog, "ff7rebirth-steam-win64-11111111", omit_ids={release_entry["id"]})
        with self.assertRaisesRegex(generator.CatalogError, "must have a value in every declared build"):
            self.load(catalog)

    def test_locator_missing_from_a_declared_build_is_rejected(self) -> None:
        catalog = self.raw()
        new_build_id = "ff7rebirth-steam-win64-44444444"
        self.add_second_build(catalog, new_build_id)
        catalog["locators"][0]["builds"].pop(new_build_id)
        with self.assertRaisesRegex(generator.CatalogError, "must be derived for every declared build"):
            self.load(catalog)

    def test_address_absent_from_every_build_is_rejected(self) -> None:
        catalog = self.raw()
        optional_entry = next(entry for entry in catalog["addresses"] if entry["requirement"] != "release")
        optional_entry["builds"] = {}
        with self.assertRaisesRegex(generator.CatalogError, "builds must be a non-empty object"):
            self.load(catalog)

    def test_literal_zero_rva_is_rejected(self) -> None:
        catalog = self.raw()
        entry = catalog["addresses"][0]
        build_id = catalog["builds"][0]["id"]
        entry["builds"][build_id]["rva"] = "0x0"
        with self.assertRaisesRegex(generator.CatalogError, "outside the executable image"):
            self.load(catalog)

    def test_absent_build_entry_renders_placeholder_and_no_inc_lines(self) -> None:
        catalog = self.raw()
        omitted_id = "piano_score_mode_setup"
        omitted_raw = next(entry for entry in catalog["addresses"] if entry["id"] == omitted_id)
        self.assertEqual(omitted_raw["requirement"], "optional")
        self.assertIsNotNone(omitted_raw["hook_spec"])
        present_build_id = catalog["builds"][0]["id"]
        new_build_id = "ff7rebirth-steam-win64-22222222"
        self.add_second_build(catalog, new_build_id, omit_ids={omitted_id})

        _, entries, _ = self.load(catalog)
        omitted_entry = next(entry for entry in entries if entry["id"] == omitted_id)
        self.assertNotIn(new_build_id, omitted_entry["builds"])

        rendered_rvas = generator.render_rvas(entries, new_build_id).decode("ascii")
        self.assertIn(f"inline constexpr uintptr_t {omitted_entry['cpp_symbol']} = 0x0;", rendered_rvas)

        rendered_signatures = generator.render_rva_signatures(entries, new_build_id).decode("ascii")
        self.assertNotIn(f'"{omitted_id}"', rendered_signatures)

        rendered_hooks = generator.render_hook_specs(entries, new_build_id).decode("ascii")
        self.assertNotIn(omitted_entry["cpp_symbol"], rendered_hooks)

        # The build where the address remains present still renders it normally.
        rendered_rvas_present = generator.render_rvas(entries, present_build_id).decode("ascii")
        self.assertIn(
            f"inline constexpr uintptr_t {omitted_entry['cpp_symbol']} = 0x03998360;", rendered_rvas_present
        )
        rendered_signatures_present = generator.render_rva_signatures(entries, present_build_id).decode("ascii")
        self.assertIn(f'"{omitted_id}"', rendered_signatures_present)
        rendered_hooks_present = generator.render_hook_specs(entries, present_build_id).decode("ascii")
        self.assertIn(omitted_entry["cpp_symbol"], rendered_hooks_present)


class MenuThemeCatalogTests(unittest.TestCase):
    def test_bottom_attribute_mapping_and_borrowed_abi_match_static_evidence(self) -> None:
        catalog = json.loads(generator.CATALOG_PATH.read_text(encoding="utf-8"))
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        evidence_path = "analysis/MenuThemeBottomAttribute-20261021.json"
        evidence = json.loads((generator.ROOT / evidence_path).read_text(encoding="utf-8"))
        self.assertEqual(len(evidence["abi"]), 3)
        self.assertEqual(evidence["layout"]["attribute_bytes"], 64)
        self.assertEqual(evidence["layout"]["color_bytes"], 40)
        self.assertIn("does not consume input", evidence["abi"]["end_text_slate_set_bottom_attribute"])
        self.assertIn("initialized destination", evidence["abi"]["native_slate_color_attribute_assign"])
        self.assertIn("without freeing caller storage", evidence["abi"]["native_slate_color_attribute_destroy"])
        for build_id, profile in evidence["profiles"].items():
            self.assertEqual(set(profile["functions"]), set(evidence["abi"]))
            for entry_id, (rva, raw) in profile["functions"].items():
                with self.subTest(build=build_id, entry=entry_id):
                    entry = entries[entry_id]
                    self.assertEqual(entry["cpp_symbol"], "".join(part.capitalize() for part in entry_id.split("_")))
                    self.assertEqual(entry["kind"], "function")
                    self.assertEqual(entry["subsystem"], "menu_theme_palette")
                    self.assertEqual(entry["requirement"], "research")
                    self.assertEqual(entry["status"], "research")
                    self.assertEqual(entry["validation"], {"policy": "signature"})
                    self.assertEqual(entry["install_policy"], "callable")
                    self.assertIsNone(entry["hook_owner"])
                    self.assertIsNone(entry["hook_spec"])
                    self.assertEqual(entry["consumers"], ["src/game/menu_theme_palette.cpp"])
                    self.assertEqual(set(entry["builds"]), set(evidence["profiles"]))
                    actual = entry["builds"][build_id]
                    self.assertEqual(int(actual["rva"], 16), int(rva, 16))
                    prefix = bytes.fromhex(raw)
                    self.assertEqual(actual["signature"], {
                        "bytes": prefix.hex(" "), "mask": "x" * len(prefix)})
                    attribution = actual["evidence"]
                    self.assertIsInstance(attribution, list)
                    self.assertTrue(attribution)
                    self.assertEqual(attribution[0]["type"], profile["evidence_type"])
                    self.assertEqual(attribution[0]["path"], evidence_path)
                    self.assertIn(evidence["abi"][entry_id], attribution[0]["detail"])
                    self.assertIn("64-byte attribute, not 40-byte SlateColor", attribution[0]["detail"])
                    self.assertEqual(sum(
                        build_id in other["builds"]
                        and int(other["builds"][build_id]["rva"], 16) == int(rva, 16)
                        for other in entries.values()), 1)

    def test_published_class_maps_and_signed_rip_targets_match_evidence(self) -> None:
        # Mapping checks stay useful while independently owned consumers are landing;
        # the normal generator tests still enforce the complete consumer validation.
        catalog = json.loads(generator.CATALOG_PATH.read_text(encoding="utf-8"))
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        evidence_path = "analysis/MenuThemeClasses-20261021.json"
        evidence = json.loads((generator.ROOT / evidence_path).read_text(encoding="utf-8"))
        recipe = evidence["reference"]
        alias = recipe["end_image_reference_reuses"]
        self.assertEqual(alias, "end_image_static_class")
        self.assertNotIn("theme_class_end_image_reference", entries)
        expected_ids = set()
        for build_id, profile in evidence["profiles"].items():
            for name, (reference_rva, slot_rva, call_disp, slot_disp) in profile["classes"].items():
                with self.subTest(build=build_id, native_class=name):
                    snake = re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()
                    slot_id = f"theme_class_{snake}"
                    reference_id = alias if name == "EndImage" else f"{slot_id}_reference"
                    expected_ids.add(slot_id)
                    if reference_id != alias:
                        expected_ids.add(reference_id)
                    slot = entries[slot_id]
                    reference = entries[reference_id]
                    self.assertEqual(slot["cpp_symbol"], f"ThemeClass{name}")
                    self.assertEqual(slot["kind"], "global")
                    self.assertEqual(slot["validation"], {"policy": "data_reference"})
                    self.assertEqual(slot["install_policy"], "data_only")
                    self.assertIsNone(slot["builds"][build_id]["signature"])
                    self.assertEqual(int(slot["builds"][build_id]["rva"], 16), int(slot_rva, 16))
                    if reference_id != alias:
                        self.assertEqual(reference["cpp_symbol"], f"ThemeClass{name}Reference")
                        self.assertEqual(reference["kind"], "call_site")
                        self.assertEqual(reference["install_policy"], "signature_only")
                    else:
                        # The retained holder-creation callable keeps its one owner;
                        # published-class admission only authenticates its bytes.
                        self.assertEqual(reference["install_policy"], "callable")
                    self.assertEqual(reference["validation"], {"policy": "signature"})
                    for entry in (slot, reference):
                        self.assertEqual(set(entry["builds"]), set(evidence["profiles"]))
                        self.assertEqual(entry["requirement"], "research")
                        self.assertEqual(entry["status"], "research")
                        self.assertIsNone(entry["hook_owner"])
                        self.assertIsNone(entry["hook_spec"])
                        self.assertEqual(entry["consumers"], ["src/game/menu_theme_native.cpp"])
                        attribution = entry["builds"][build_id]["evidence"]
                        self.assertIsInstance(attribution, list)
                        self.assertTrue(attribution)
                        self.assertEqual(attribution[0]["type"], profile["evidence_type"])
                        if entry is not reference or reference_id != alias or build_id == catalog["default_build"]:
                            self.assertEqual(attribution[0]["path"], evidence_path)
                    actual = reference["builds"][build_id]
                    self.assertEqual(int(actual["rva"], 16), int(reference_rva, 16))
                    prefix = bytes.fromhex(recipe["prefix_template"].format(
                        call_disp_le=call_disp, slot_disp_le=slot_disp))
                    self.assertEqual(len(prefix), recipe["length"])
                    self.assertEqual(actual["signature"], {
                        "bytes": prefix.hex(" "), "mask": "x" * len(prefix)})
                    offset = recipe["rip_displacement_offset"]
                    displacement = int.from_bytes(prefix[offset:offset + 4], "little", signed=True)
                    target = int(actual["rva"], 16) + recipe["rip_instruction_end"] + displacement
                    self.assertEqual(target, int(slot_rva, 16))
                    # The alias must not conceal a second ID at the same address.
                    for rva in (reference_rva, slot_rva):
                        self.assertEqual(sum(
                            build_id in entry["builds"]
                            and int(entry["builds"][build_id]["rva"], 16) == int(rva, 16)
                            for entry in entries.values()), 1)
        self.assertEqual({entry_id for entry_id in entries if entry_id.startswith("theme_class_")},
                         expected_ids)
        default_detail = entries[alias]["builds"][catalog["default_build"]]["evidence"][0]["detail"]
        self.assertIn("native size 0x1e8 in BOTH builds", default_detail)
        self.assertIn("0x200 belongs to independently qualified EndPieImage", default_detail)

    def test_full_style_contracts_have_exact_prefixes_and_independent_build_attribution(self) -> None:
        catalog = json.loads(generator.CATALOG_PATH.read_text(encoding="utf-8"))
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        evidence_path = "analysis/MenuThemeStyles-20261021.json"
        evidence = json.loads((generator.ROOT / evidence_path).read_text(encoding="utf-8"))
        self.assertEqual(len(evidence["functions"]), 30)
        self.assertEqual(set(evidence["older_functions"]), set(evidence["functions"]))
        for entry_id, expected in evidence["functions"].items():
            with self.subTest(entry=entry_id):
                entry = entries[entry_id]
                self.assertEqual(entry["cpp_symbol"], "".join(part.capitalize() for part in entry_id.split("_")))
                self.assertEqual(entry["kind"], "function")
                self.assertEqual(entry["validation"], {"policy": "signature"})
                self.assertEqual(entry["requirement"], "research")
                self.assertEqual(entry["status"], "research")
                expected_builds = {evidence["build_id"], evidence["older_build_id"]}
                self.assertEqual(set(entry["builds"]), expected_builds)
                for build_id in expected_builds:
                    actual = entry["builds"][build_id]
                    default = build_id == evidence["build_id"]
                    contract = expected if default else evidence["older_functions"][entry_id]
                    self.assertEqual(int(actual["rva"], 16), int(contract["rva"], 16))
                    prefix = bytes.fromhex(contract["prefix"])
                    self.assertEqual(actual["signature"], {
                        "bytes": prefix.hex(" "), "mask": "x" * len(prefix)})
                    attribution = actual["evidence"]
                    self.assertIsInstance(attribution, list)
                    self.assertTrue(attribution)
                    self.assertEqual(attribution[0]["type"],
                                     evidence["evidence_type"] if default else evidence["older_evidence_type"])
                    self.assertEqual(attribution[0]["path"], evidence_path)
                    self.assertTrue(attribution[0]["detail"])
                    if default:
                        self.assertIn(expected["abi"], attribution[0]["detail"])
                    else:
                        detail = attribution[0]["detail"]
                        self.assertIn(evidence["older_program"], detail)
                        self.assertIn("older_layout_verification", detail)
                        self.assertIn("no older disk/runtime qualification", detail)
                        if "registered_thunk_rva" in contract:
                            self.assertIn(contract["registered_thunk_rva"], detail)
                            self.assertNotEqual(int(actual["rva"], 16),
                                                int(contract["registered_thunk_rva"], 16))
                        if entry_id in ("end_tiled_image_synchronize", "end_image_set_synchronize"):
                            self.assertIn("0x2a8", detail)
                            self.assertIn("0x2b0", detail)
                            self.assertIn("direct target", detail)
        tint = evidence["layout"]["SlateBrushTint"]
        brush_detail = entries["native_slate_brush_assign"]["builds"][evidence["older_build_id"]]["evidence"][0]["detail"]
        self.assertIn("layout.SlateBrushTint", brush_detail)
        self.assertIn(f"+{tint['offset']}", brush_detail)
        self.assertIn(f"size {tint['size']}", brush_detail)

    def test_palette_hook_ownership_follows_actual_property_and_opacity_contracts(self) -> None:
        catalog = json.loads(generator.CATALOG_PATH.read_text(encoding="utf-8"))
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        source = (generator.ROOT / "src/game/menu_theme_palette.cpp").read_text(encoding="utf-8")
        array = source.split("constexpr Contract contracts[] = {", 1)[1].split("};", 1)[0]
        contracts = re.findall(
            r'\{"([a-z_]+)", Property::\w+, (0x[0-9a-f]+), (true|false), (true|false)\}', array)
        self.assertEqual(len(contracts), 17)
        self.assertEqual(sum(opacity == "false" for _, _, _, opacity in contracts), 11)
        self.assertEqual(sum(opacity == "true" for _, _, _, opacity in contracts), 6)
        hook_ids = {entry_id for entry_id, _, _, _ in contracts}
        self.assertEqual({entry["id"] for entry in entries.values()
                          if entry["hook_owner"] == "menu_theme_palette"}, hook_ids)
        previous_max = max(entry["hook_spec"]["order"] for entry in entries.values()
                           if entry["hook_spec"] and entry["id"] not in hook_ids)
        evidence = json.loads((generator.ROOT / "analysis/MenuThemeStyles-20261021.json").read_text(encoding="utf-8"))
        for index, (entry_id, offset, owning, opacity) in enumerate(contracts):
            with self.subTest(entry=entry_id):
                entry = entries[entry_id]
                self.assertEqual(entry["subsystem"], "menu_theme_palette")
                self.assertEqual(entry["install_policy"], "optional_hook")
                self.assertEqual(entry["hook_owner"], "menu_theme_palette")
                self.assertEqual(entry["consumers"], ["src/game/menu_theme_palette.cpp"])
                self.assertEqual(entry["hook_spec"], {
                    "name": entry_id, "order": previous_max + 1 + index,
                    "required_for_release_startup": False})
                contract = evidence["functions"][entry_id]
                if opacity == "false":
                    self.assertEqual(int(contract["field"], 16), int(offset, 16))
                    self.assertIn("consuming SlateColor" if owning == "true" else "borrowed LinearColor",
                                  contract["abi"])
                else:
                    self.assertIn("XMM1 float", contract["abi"])
        for entry_id in set(evidence["functions"]) - hook_ids:
            with self.subTest(helper=entry_id):
                entry = entries[entry_id]
                self.assertEqual(entry["install_policy"], "callable")
                self.assertIsNone(entry["hook_owner"])
                self.assertIsNone(entry["hook_spec"])
                consumers = ["src/game/menu_theme_native.cpp"]
                if entry_id.startswith("native_slate_color_"):
                    if entry_id != "native_slate_color_assign":
                        consumers.clear()
                    consumers.append("src/game/menu_theme_palette.cpp")
                self.assertEqual(entry["consumers"], consumers)

    def test_containment_read_contract_matches_static_evidence(self) -> None:
        catalog, _, _ = generator.load_and_validate_catalog()
        evidence_path = "analysis/MenuThemeContainment-20261020.json"
        evidence = json.loads((generator.ROOT / evidence_path).read_text(encoding="utf-8"))
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        reference = evidence["contract"]["class_slot_reference"]
        for build_id, profile in evidence["profiles"].items():
            for entry_id, expected in profile["addresses"].items():
                with self.subTest(entry=entry_id, build=build_id):
                    entry = entries[entry_id]
                    actual = entry["builds"][build_id]
                    self.assertEqual(entry["subsystem"], "menu_theme_containment")
                    self.assertIsNone(entry["hook_owner"])
                    self.assertIsNone(entry["hook_spec"])
                    self.assertEqual(entry["install_policy"],
                                     "data_only" if entry["kind"] == "global" else "signature_only")
                    self.assertEqual(int(actual["rva"], 16), int(expected["rva"], 16))
                    self.assertEqual(actual["evidence"][0]["type"], profile["evidence_type"])
                    self.assertEqual(actual["evidence"][0]["path"], evidence_path)
                    if "prefix" in expected:
                        raw = bytes.fromhex(expected["prefix"])
                        self.assertEqual(bytes.fromhex(actual["signature"]["bytes"]), raw)
                        self.assertEqual(actual["signature"]["mask"], "x" * len(raw))
                    else:
                        self.assertIsNone(actual["signature"])
                        self.assertEqual(expected["width"], 8)
            for role in ("panel_widget", "panel_slot"):
                site = profile["addresses"][f"native_{role}_class_read_site"]
                offset = reference["disp32_offset"]
                raw = bytes.fromhex(site["prefix"])
                displacement = int.from_bytes(raw[offset:offset + 4], "little", signed=True)
                target = int(site["rva"], 16) + reference["instruction_end"] + displacement
                self.assertEqual(target, int(profile["addresses"][f"native_{role}_class"]["rva"], 16))

    def test_observation_sites_are_read_only_and_reference_their_globals(self) -> None:
        catalog, _, _ = generator.load_and_validate_catalog()
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        observer = [entry for entry in entries.values()
                    if entry["subsystem"] == "menu_theme_observation"]
        self.assertTrue(observer)
        for entry in observer:
            self.assertIsNone(entry["hook_owner"])
            self.assertIsNone(entry["hook_spec"])
            self.assertEqual(entry["status"], "research")
            self.assertEqual(entry["install_policy"],
                             "data_only" if entry["kind"] == "global" else "signature_only")

        references = (
            ("native_game_thread_publish_site", 3, 7, "native_game_thread_initialized"),
            ("native_game_thread_publish_site", 9, 13, "native_game_thread_id"),
            ("native_gc_flag_exchange_site", 2, 6, "native_garbage_collecting"),
            ("native_exit_purge_publish_site", 2, 7, "native_uobject_exit_purge"),
            ("native_uobject_thread_role_site", 3, 7, "native_game_thread_initialized"),
        )
        for site_id, displacement_offset, instruction_end, target_id in references:
            for build_id, site in entries[site_id]["builds"].items():
                with self.subTest(site=site_id, build=build_id):
                    raw = bytes.fromhex(site["signature"]["bytes"])
                    displacement = int.from_bytes(raw[displacement_offset:displacement_offset + 4],
                                                  "little", signed=True)
                    target = int(site["rva"], 16) + instruction_end + displacement
                    self.assertEqual(target, int(entries[target_id]["builds"][build_id]["rva"], 16))

    def test_observation_mapping_and_literal_tls_match_static_evidence(self) -> None:
        catalog, _, _ = generator.load_and_validate_catalog()
        evidence_path = "analysis/MenuThemeObservation-20261020.json"
        evidence = json.loads((generator.ROOT / evidence_path).read_text(encoding="utf-8"))
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        tls = evidence["tls"]
        self.assertEqual(tls["slot"], 0)
        self.assertTrue(tls["no_dll_tls_or_pe_tls_index_fallback"])
        for build_id, profile in evidence["profiles"].items():
            for entry_id, expected in (profile["globals"] | profile["sites"]).items():
                with self.subTest(entry=entry_id, build=build_id):
                    actual = entries[entry_id]["builds"][build_id]
                    self.assertEqual(int(actual["rva"], 16), int(expected["rva"], 16))
                    self.assertEqual(actual["evidence"][0]["type"], profile["evidence_type"])
                    self.assertEqual(actual["evidence"][0]["path"], evidence_path)
                    if "prefix" in expected:
                        raw = bytes.fromhex(expected["prefix"])
                        self.assertEqual(bytes.fromhex(actual["signature"]["bytes"]), raw)
                        self.assertEqual(actual["signature"]["mask"], "x" * len(raw))
                    else:
                        self.assertIsNone(actual["signature"])
            role = entries["native_uobject_thread_role_site"]["builds"][build_id]
            raw = bytes.fromhex(role["signature"]["bytes"])
            self.assertEqual(raw[tls["verifier_tail_offset"]:], bytes.fromhex(tls["verifier_tail"]))

    def test_older_bridge_mapping_matches_retained_static_evidence(self) -> None:
        catalog, _, _ = generator.load_and_validate_catalog()
        evidence_path = "analysis/MenuThemeNative-OlderBuild-20261020.json"
        evidence = json.loads((generator.ROOT / evidence_path).read_text(encoding="utf-8"))
        build_id = evidence["build_id"]
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        bridge = [entry for entry in entries.values()
                  if entry["subsystem"] == "menu_theme_native"
                  and entry["id"] in evidence["proposed_addresses"]]
        self.assertTrue(bridge)
        for entry in bridge:
            with self.subTest(entry=entry["id"]):
                expected = evidence["proposed_addresses"][entry["id"]]
                actual = entry["builds"][build_id]
                self.assertEqual(int(actual["rva"], 16), int(expected["rva"], 16))

                self.assertEqual(actual["evidence"][0]["type"], "static_import_contract")
                self.assertEqual(actual["evidence"][0]["path"], evidence_path)
                if expected["prefix"] is None:
                    self.assertIsNone(actual["signature"])
                    self.assertEqual(entry["install_policy"], "data_only")
                else:
                    prefix = bytes.fromhex(expected["prefix"])
                    self.assertEqual(bytes.fromhex(actual["signature"]["bytes"]), prefix)
                    self.assertEqual(actual["signature"]["mask"], "x" * len(prefix))
        for proposed_id, expected in evidence["proposed_addresses"].items():
            if "reuse_catalog_id" in expected:
                self.assertNotIn(proposed_id, entries)
                actual = entries[expected["reuse_catalog_id"]]["builds"][build_id]
                self.assertEqual(int(actual["rva"], 16), int(expected["rva"], 16))

    def test_fresh_texture_policy_matches_native_bool_metadata(self) -> None:
        catalog, _, _ = generator.load_and_validate_catalog()
        path = "analysis/MenuThemeTexturePolicy-20261021.json"
        evidence = json.loads((generator.ROOT / path).read_text(encoding="utf-8"))
        setter = evidence["setter"]
        entry = next(item for item in catalog["addresses"] if item["id"] == setter["id"])
        self.assertEqual(entry["install_policy"], "callable")
        self.assertIsNone(entry["hook_owner"])
        self.assertIsNone(entry["hook_spec"])
        self.assertEqual(evidence["properties"]["SRGB"]["mask"], "0x01")
        self.assertEqual(evidence["properties"]["bNotOfflineProcessed"]["mask"], "0x10")
        for build_id, expected in setter["profiles"].items():
            with self.subTest(build=build_id):
                actual = entry["builds"][build_id]
                self.assertEqual(int(actual["rva"], 16), int(expected["rva"], 16))
                raw = bytes.fromhex(actual["signature"]["bytes"])
                self.assertEqual(raw, bytes.fromhex(setter["prefix"]))
                self.assertEqual(actual["signature"]["mask"], "x" * len(raw))
                self.assertEqual(actual["evidence"][0]["type"], expected["evidence_type"])
                self.assertEqual(actual["evidence"][0]["path"], path)


if __name__ == "__main__":
    unittest.main()
