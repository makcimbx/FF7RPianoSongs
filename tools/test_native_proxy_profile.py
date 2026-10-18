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
