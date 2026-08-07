from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
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

    def locator(self) -> dict:
        return copy.deepcopy(self.catalog["locators"][0])

    def validate(self, locator: dict) -> list[dict]:
        return generator.validate_locators([locator], generator.ROOT)

    def test_locator_schema_is_strict(self) -> None:
        locator = self.locator()
        locator["unexpected"] = True
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
        invalid_character["pattern"]["mask"] = "x.....xxxxxx????xx"
        with self.assertRaisesRegex(generator.CatalogError, "only 'x' and '\\?'"):
            self.validate(invalid_character)

        wrong_length = self.locator()
        wrong_length["pattern"]["mask"] = "x?????xxxxxx????x"
        with self.assertRaisesRegex(generator.CatalogError, "lengths differ"):
            self.validate(wrong_length)

    def test_rel32_and_instruction_offsets_must_fit_pattern(self) -> None:
        rel32_out_of_bounds = self.locator()
        rel32_out_of_bounds["decode"]["displacement_offset"] = "0x0f"
        with self.assertRaisesRegex(generator.CatalogError, "rel32 displacement is outside"):
            self.validate(rel32_out_of_bounds)

        instruction_out_of_bounds = self.locator()
        instruction_out_of_bounds["decode"]["instruction_size"] = "0x13"
        with self.assertRaisesRegex(generator.CatalogError, "instruction_size is outside"):
            self.validate(instruction_out_of_bounds)

    def test_rel32_displacement_must_fit_instruction(self) -> None:
        displacement_outside_instruction = self.locator()
        displacement_outside_instruction["decode"]["instruction_size"] = "0x05"
        with self.assertRaisesRegex(generator.CatalogError, "rel32 displacement is outside the instruction"):
            self.validate(displacement_outside_instruction)

    def test_adjustments_reject_duplicates_and_require_zero_first(self) -> None:
        duplicate = self.locator()
        duplicate["candidate_adjustments"] = ["0x00", "0x24", "0x24"]
        with self.assertRaisesRegex(generator.CatalogError, "must be unique"):
            self.validate(duplicate)

        reordered_zero = self.locator()
        reordered_zero["candidate_adjustments"] = ["0x24", "0x00", "0x20"]
        with self.assertRaisesRegex(generator.CatalogError, "begin with zero"):
            self.validate(reordered_zero)

    def test_nonzero_adjustment_order_is_preserved_without_sorting(self) -> None:
        locator = self.locator()
        locator["candidate_adjustments"] = ["0x00", "0x10", "0x30", "0x24", "0x20"]
        parsed = self.validate(locator)
        rendered = generator.render_runtime_locator_specs(parsed).decode("ascii")
        self.assertIn("{{0x00, 0x10, 0x30, 0x24, 0x20}}", rendered)

    def test_generated_api_contains_catalog_data(self) -> None:
        rendered = generator.render_runtime_locator_specs(self.locators).decode("ascii")
        self.assertIn("kGuObjectArrayPatternBytes", rendered)
        self.assertIn('"x?????xxxxxx????xx"', rendered)
        self.assertIn("RuntimeLocatorMatchPolicy::First", rendered)
        self.assertIn("RuntimeLocatorDecodeKind::Rel32, 2, 6", rendered)
        self.assertIn("{{0x00, 0x24, 0x20, 0x10, 0x30}}", rendered)

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
        return catalog

    def test_release_entry_missing_from_a_build_is_rejected(self) -> None:
        catalog = self.raw()
        release_entry = next(entry for entry in catalog["addresses"] if entry["requirement"] == "release")
        self.add_second_build(catalog, "ff7rebirth-steam-win64-11111111", omit_ids={release_entry["id"]})
        with self.assertRaisesRegex(generator.CatalogError, "must have a value in every declared build"):
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
