from __future__ import annotations

import copy
import importlib.util
import json
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
    """Schema-3 multi-build coverage and absence-handling invariants."""

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
            if entry["id"] in omit_ids:
                continue
            entry["builds"][new_build_id] = copy.deepcopy(entry["builds"][source_build_id])
        # Locators admit no absence, so a new build must always carry its own derivation.
        for locator in catalog["locators"]:
            locator["builds"][new_build_id] = copy.deepcopy(locator["builds"][source_build_id])
        return catalog

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


if __name__ == "__main__":
    unittest.main()
