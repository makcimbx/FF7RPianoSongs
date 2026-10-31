import copy
import json
from pathlib import Path
import unittest

from generate_native_proxy_profile import render


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.catalog = json.loads((Path(__file__).resolve().parents[1] / "src/game/rva_catalog.json").read_text(encoding="utf-8"))

    def test_both_catalog_profiles(self):
        for build in self.catalog["builds"]:
            result = render(self.catalog, build["id"])
            self.assertIn(build["id"], result)
            self.assertIn("kEntryBytes", result)
            self.assertIn(build["pe_checksum"], result)

    def test_unknown_or_duplicate_build(self):
        with self.assertRaises((ValueError, KeyError)):
            render(self.catalog, "unknown")
        self.catalog["builds"].append(copy.deepcopy(self.catalog["builds"][0]))
        with self.assertRaises(ValueError):
            render(self.catalog, self.catalog["builds"][0]["id"])

    def test_per_build_phase_and_no_imported_promotion(self):
        post = [b for b in self.catalog["builds"] if b["runtime_profile"]["owned_proxy"]["phase"] == "pre_winmain"]
        legacy = [b for b in self.catalog["builds"] if b["runtime_profile"]["owned_proxy"]["phase"] == "entry"]
        self.assertEqual((len(post), len(legacy)), (1, 1))
        self.assertIn("kOwnedCallEnabled = true", render(self.catalog, post[0]["id"]))
        self.assertIn("kOwnedCallEnabled = false", render(self.catalog, legacy[0]["id"]))
        legacy[0]["runtime_profile"]["owned_proxy"] = copy.deepcopy(post[0]["runtime_profile"]["owned_proxy"])
        with self.assertRaisesRegex(ValueError, "matching-disk"):
            render(self.catalog, legacy[0]["id"])

    def test_call_authority_exact_target_and_disk_contract(self):
        build = next(b for b in self.catalog["builds"] if b["runtime_profile"]["owned_proxy"]["phase"] == "pre_winmain")
        ident = build["id"]
        row = next(a for a in self.catalog["addresses"] if a["id"] == "native_bootstrap_show_to_winmain")["builds"][ident]
        original = copy.deepcopy(row)
        for mutation in ("mask", "opcode", "displacement", "disk"):
            row.clear(); row.update(copy.deepcopy(original))
            if mutation == "mask": row["signature"]["mask"] = "?" * len(bytes.fromhex(row["signature"]["bytes"]))
            elif mutation == "disk": row["evidence"] = []
            else:
                data = bytearray.fromhex(row["signature"]["bytes"])
                data[0 if mutation == "opcode" else 1] ^= 1
                row["signature"]["bytes"] = data.hex(" ")
            with self.assertRaises(ValueError): render(self.catalog, ident)
        row.clear(); row.update(original)
        build["runtime_profile"]["owned_proxy"]["patch_bytes"] = 6
        with self.assertRaises(ValueError): render(self.catalog, ident)

    def test_nonexact_or_outside_entry(self):
        address = next(a for a in self.catalog["addresses"] if a["id"] == "native_audio_exe_entry")
        build_id, entry = next(iter(address["builds"].items()))
        entry["signature"]["mask"] = "?"
        with self.assertRaises(ValueError):
            render(self.catalog, build_id)
        entry["signature"]["mask"] = "x" * len(bytes.fromhex(entry["signature"]["bytes"]))
        entry["rva"] = "0xffffffff"
        with self.assertRaises(ValueError):
            render(self.catalog, build_id)


if __name__ == "__main__":
    unittest.main()
