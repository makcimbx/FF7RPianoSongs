"""Retained bootstrap address migration; no runtime/process qualification."""
import json
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = "ff7rebirth-steam-win64-6a16ced2"
OLDER = "ff7rebirth-steam-win64-68fd6fde"
OLD_PROFILE = json.loads((ROOT / "analysis/NativeAudioBootstrapProfile-OlderBuild-20261017.json").read_text())
OLDER_BOOTSTRAP_RVAS = OLD_PROFILE["canonical_counterparts"]
CODE = {
    "cookie": "0216c76c", "crt": "0216bf30", "crt_complete": "0216bfac",
    "show": "0216c660", "show_to_winmain": "0216c015", "winmain": "027e9530",
    "hinstance_store": "027e9556", "cookie_time": "0216c79b",
    "task_publish": "043d5918", "task_call": "027e7239", "crash_entry": "04487c1c",
}
DATA = {
    "cookie_value": "06ddc800", "hinstance": "08f48ee0", "task_graph": "08f38150",
    "pool": "08f38598", "background_pool": "08f38590", "gu_serial": "0871ec50",
    "crash_stop": "06eff320", "crash_process": "06eff358", "crash_result": "09274b78",
    "crash_complete": "06f12858", "crash_worker": "06eff318", "crash_request": "06eff328",
    "crash_event": "06eff330",
}
CODE.update({
    "srw_header": "0216b4d8", "srw_guard_call": "019daa59", "srw_malloc_secondary": "024bb500",
    "srw_pool_secondary": "024d20b0", "srw_pool_call": "02222f05", "srw_block_call": "00b523b3",
    "srw_name_call": "00b51900", "srw_first_name": "00352bb0", "srw_initterm_call": "0216bf99",
    "srw_reserve_code": "016ec470", "srw_malloc_code": "0079fa7c", "srw_name_destination": "00b517f9",
    "srw_initterm_thunk": "0216dc96",
})
DATA.update({
    "srw_guard": "09190194", "srw_gmalloc": "08f405e0", "srw_pool_complete": "08f309f8",
    "srw_reserve": "09274ba8", "srw_lock": "06fe2038", "srw_output": "06f52300", "srw_pool": "090d3c00",
    "srw_init_begin": "0556a058", "srw_init_end": "055e4a28", "srw_init_iat": "05569bc0",
    "srw_init_name": "06c904ac", "srw_crt_setup": "0216bf14", "srw_atexit": "00363d3c",
})


class BootstrapCatalogTests(unittest.TestCase):
    def test_game_windows_match_retained_source_evidence(self):
        profile = json.loads((ROOT / "analysis/NativeAudioBootstrapProfile-20261016.json").read_text())
        windows = {int(w["rva"], 16): w["bytes"] for w in profile["code_windows"].values()}
        srw = json.loads((ROOT / "analysis/NativeAudioFirstSrwTrigger-20261017.json").read_text())
        windows.update({int(w["rva"], 16): w["bytes"] for w in srw["code_windows"]})
        imported = srw["crt_callback_frame_contract"]["exe_import"]
        windows[int(imported["thunk_rva"], 16)] = imported["thunk_bytes"]
        entries = json.loads((ROOT / "src/game/rva_catalog.json").read_text())["addresses"]
        for e in entries:
            if e["id"].startswith("native_bootstrap_") and e["id"].removeprefix("native_bootstrap_") in CODE:
                contract = e["builds"][BUILD]
                self.assertEqual(bytes.fromhex(contract["signature"]["bytes"]).hex(), windows[int(contract["rva"], 16)])

    def test_external_loader_windows_match_retained_source_evidence(self):
        profile = json.loads((ROOT / "analysis/NativeAudioLoaderHandoff-20261016.json").read_text())
        expected = {}
        def walk(value):
            if isinstance(value, dict):
                if "window_rva" in value and "window_expected" in value:
                    expected[int(value["window_rva"], 16)] = value["window_expected"]
                if "call_rva" in value and "call_expected" in value:
                    expected[int(value["call_rva"], 16)] = value["call_expected"]
                for child in value.values():
                    walk(child)
            elif isinstance(value, list):
                for child in value:
                    walk(child)
        walk(profile)
        export = profile["export_handoff"]
        expected[int(export["runtime_function"]["begin_rva"], 16)] = export["runtime_function"]["entry_window"]
        expected[int(export["export_name_rva"], 16)] = b"InitializeASI\0".hex()
        source = (ROOT / "src/game/native_audio_bootstrap.cpp").read_text()
        body = re.search(r'constexpr Window loader_windows\[\]=\{(.*?)\};', source, re.S).group(1)
        compiled = {int(rva, 16): raw for rva, raw in re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"\}', body)}
        self.assertTrue(compiled)
        for rva, raw in compiled.items():
            self.assertEqual(raw, expected[rva])
        self.assertIn(profile["currentLoaderDiskSHA"], source)

    def test_external_crt_is_exact_reviewed_fixture_not_system32_wildcard(self):
        profile = json.loads((ROOT / "analysis/NativeAudioFirstSrwTrigger-20261017.json").read_text())
        crt = profile["crt_callback_frame_contract"]["current_os_disk_fixture"]
        source = (ROOT / "src/game/native_audio_bootstrap.cpp").read_text()
        body = re.search(r'constexpr Window crt_windows\[\]=\{(.*?)\};', source, re.S).group(1)
        compiled = {int(rva, 16): raw for rva, raw in re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"\}', body)}
        self.assertEqual(compiled, {int(w["rva"], 16): w["bytes"] for w in crt["code_windows"]})
        self.assertIn(crt["sha256"], source)
        self.assertIn("RtlVirtualUnwind(UNW_FLAG_NHANDLER", source)
        self.assertNotRegex(source, r'\b(?:CreateThread|_beginthreadex|LoadLibraryW|LoadLibraryA)\s*\(')

    def test_each_measured_build_has_its_own_nonhook_contracts(self):
        entries = json.loads((ROOT / "src/game/rva_catalog.json").read_text())["addresses"]
        selected = {e["id"].removeprefix("native_bootstrap_"): e for e in entries
                    if e["id"].startswith("native_bootstrap_")}
        runtime_cold = {"hinstance", "task_graph", "pool", "background_pool", "gu_serial",
                        "crash_stop", "crash_process", "crash_result", "crash_complete",
                        "crash_worker", "crash_request", "crash_event", "srw_guard",
                        "srw_gmalloc", "srw_pool_complete", "srw_reserve", "srw_lock", "srw_output"}
        self.assertEqual(set(selected), CODE.keys() | DATA.keys() | {"io_pool"})
        for name, rva in (CODE | DATA).items():
            with self.subTest(name=name):
                e = selected[name]
                self.assertEqual(set(e["builds"]), {BUILD, OLDER})
                self.assertEqual(e["builds"][BUILD]["rva"], "0x" + rva)
                old = e["builds"][OLDER]
                self.assertEqual(old["rva"], OLDER_BOOTSTRAP_RVAS[e["id"]])
                self.assertEqual(old["evidence"][0]["type"], "static_import_contract")
                self.assertEqual(old["evidence"][0]["path"], "analysis/NativeAudioBootstrapProfile-OlderBuild-20261017.json")
                self.assertIsNone(e["hook_owner"])
                self.assertIsNone(e["hook_spec"])
                self.assertEqual(e["consumers"], ["src/game/native_audio_bootstrap.cpp"]
                                 + (["src/game/native_audio_runtime.cpp"] if name in runtime_cold else []))
                self.assertEqual(e["install_policy"], "signature_only" if name in CODE else "data_only")
                signature = e["builds"][BUILD]["signature"]
                if name in CODE:
                    self.assertEqual(len(bytes.fromhex(signature["bytes"])), len(signature["mask"]))
                    self.assertEqual(set(signature["mask"]), {"x"})
                    self.assertEqual(bytes.fromhex(old["signature"]["bytes"]).hex(), OLD_PROFILE["code_windows"][e["id"]])
                else:
                    self.assertIsNone(signature)
                    self.assertIsNone(old["signature"])
        io = selected["io_pool"]
        self.assertEqual(io["consumers"], ["src/game/native_audio_bootstrap.cpp", "src/game/native_audio_runtime.cpp"])
        self.assertEqual(set(io["builds"]), {OLDER})
        self.assertEqual(io["builds"][OLDER]["rva"], "0x09104da0")
        self.assertIsNone(io["hook_owner"])

    def test_all_older_counterparts_and_decoder_are_exact_imported_contracts(self):
        entries = {e["id"]: e for e in json.loads((ROOT / "src/game/rva_catalog.json").read_text())["addresses"]}
        self.assertEqual(len(OLDER_BOOTSTRAP_RVAS), 53)
        for name, address in OLDER_BOOTSTRAP_RVAS.items():
            with self.subTest(name=name):
                record = entries[name]["builds"][OLDER]
                self.assertEqual(record["rva"], address)
                self.assertEqual(record["evidence"][0]["type"], "static_import_contract")
                # Setup/atexit are table-value witnesses, never callable/code authority.
                if record["signature"] is not None:
                    self.assertEqual(bytes.fromhex(record["signature"]["bytes"]).hex(), OLD_PROFILE["code_windows"][name])
        decoder = entries["native_oodle_decompress"]
        self.assertEqual(set(decoder["builds"]), {BUILD, OLDER})
        self.assertEqual(decoder["builds"][OLDER]["rva"], OLD_PROFILE["game_contained_oodle"]["decoder_rva"])
        self.assertEqual(bytes.fromhex(decoder["builds"][OLDER]["signature"]["bytes"]).hex(),
                         OLD_PROFILE["game_contained_oodle"]["complete_entry_signature"])
        self.assertEqual(decoder["install_policy"], "callable")
        self.assertEqual(decoder["consumers"], ["src/game/native_audio_decoder.cpp"])
        self.assertIsNone(decoder["hook_owner"])
        self.assertIsNone(decoder["hook_spec"])

    def test_current_game_decoder_matches_independently_recovered_disk_contract(self):
        entries = {e["id"]: e for e in json.loads((ROOT / "src/game/rva_catalog.json").read_text())["addresses"]}
        profile = json.loads((ROOT / "analysis/NativeAudioGameDecoder-20261017.json").read_text())["builds"][BUILD]
        record = entries["native_oodle_decompress"]["builds"][BUILD]
        self.assertEqual(record["rva"], profile["rva"])
        self.assertEqual(record["signature"], profile["signature"])
        self.assertEqual(record["evidence"][0]["type"], "static_disk_contract")
        self.assertEqual(record["evidence"][0]["path"], "analysis/NativeAudioGameDecoder-20261017.json")
        self.assertEqual(len(profile["disk_import_comparisons"]), 2)
        self.assertGreater(profile["disk_import_comparisons"][0]["bytes"], len(bytes.fromhex(record["signature"]["bytes"])))

    def test_entry_owner_and_runtime_module_are_not_a_second_bootstrap_hook(self):
        entries = {e["id"]: e for e in json.loads((ROOT / "src/game/rva_catalog.json").read_text())["addresses"]}
        entry = entries["native_audio_exe_entry"]
        self.assertEqual(entry["hook_owner"], "native_audio_runtime")
        self.assertGreater(entry["hook_spec"]["order"], entries["native_audio_updater"]["hook_spec"]["order"])
        for identifier in ("native_audio_exe_entry", "native_audio_crt_state", "native_audio_initial_gu_header",
                           "package_store_owner_global", "package_store_generation", "dataobject_name_map",
                           "bgmlist_registry", "scoreinfo_registry", "pianoscore_registry"):
            self.assertIn("src/game/native_audio_bootstrap.cpp", entries[identifier]["consumers"])


if __name__ == "__main__":
    unittest.main()
