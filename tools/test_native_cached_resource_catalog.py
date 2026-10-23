"""Retained cached-resource addresses/signatures; no dispatch authority.

Reviewed 1.004 counterparts are imported-Ghidra/PE-correlated static evidence,
NOT older disk SHA or runtime validation. Separate bootstrap evidence supplies
the exact older startup profile; these address fixtures alone grant no authority.

Migrated from parent workspace Tools/Frida/{cache_mount_layout,mount_sites,
music_load_layout,bgm_registration_layout,scoreinfo_binding_layout,
pianoscore_binding_layout,piano_pair_query_layout}.json. These signatures were
disk-correlated there, not rediscovered or inferred from another build here.
The bounded successful route is retained in Tools/Frida/captures/
piano-pair-session-7yc1prw7/native-start-pause-resume-summary.json and
process-exit-disposition.json. Frida foreign-entry exclusion is NOT an ASI gate.

This standalone fixture preserves the migrated facts for catalog regression
checks; it neither loads the game nor authorizes calls, hooks, or registration.

Persistent-seam migration, independently disk checked 2026-10-16 against SHA256
752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc:
factory 044d3d4c publishes owner+8 at 0907beb0 and increments DWORD 091d9170.
Binder 044ce048 transfers a 40-byte owning closure through 010f0240 into the
dispatcher.impl+1432 event (data/count/cap 0/8/12, entries16 pointer/count0/8).
Closure vtable057a17b0, ID16, PackageStore receiver24=owner+680, callback32=
044d5dcc. Owner vtables05d232e0/05d23310, dispatcher owner+648. Factory,
shutdown04539fdc and destructor044cfc98 invalidate BEFORE their originals.
Updater0136fc34 void(receiver) leaves its receiver+8 critical section before
normal return; qualified caller-return0136fbda, disk bytes c5f8287424204883c4305be902000000.
Static Ghidra crosschecks supplied the semantic contracts; selected function
windows and all three vtable entries were independently read from this disk.
BSS globals have no raw disk payload and are correlated through instructions,
not represented as invented file bytes. Missed initializer is unavailable;
pointer equality alone never restores mount authority after invalidation.
"""

import json
import unittest
from pathlib import Path
from test_native_bootstrap_catalog import (CODE as BOOTSTRAP_CODE, DATA as BOOTSTRAP_DATA,
                                         OLDER_BOOTSTRAP_RVAS)

ROOT = Path(__file__).resolve().parents[1]
BUILD = "ff7rebirth-steam-win64-6a16ced2"
OLDER_BUILD = "ff7rebirth-steam-win64-68fd6fde"
OLDER_EVIDENCE = "analysis/NativeCachedAudioOffsets-OlderBuild-20261016.json"
UNMAPPED_OLDER = {"native_audio_exe_entry", "native_audio_crt_state", "native_audio_initial_gu_header"} | {
    "native_bootstrap_" + name for name in BOOTSTRAP_CODE.keys() | BOOTSTRAP_DATA.keys()
}
UNMAPPED_OLDER -= OLDER_BOOTSTRAP_RVAS.keys()
# Retained native evidence, not installed audio orchestration. The catalog keeps
# these regression consumers because it requires a real consumer for each entry.
RETIRED_AUDIO_BINDINGS = {
    "sqexsead_play_setup": "signature_only",
    "bgm_slot_stop": "signature_only",
    "bgm_playback_transfer": "signature_only",
    "bgm_slot_play": "signature_only",
    "bgm_prepare": "signature_only",
    "bgm_controller_lookup": "signature_only",
    "bgm_manager_pause": "signature_only",
    "bgm_slot_pause_transition": "signature_only",
    "bgm_slot_resume_transition": "signature_only",
    "piano_audio_state_tick": "signature_only",
    "piano_adaptive_judgment": "signature_only",
    "bgm_slot_setup": "callable",
    "piano_audio_request": "callable",
    "bgm_controller_key_global": "data_only",
    "piano_audio_global": "data_only",
    "static_construct_object": "signature_only",
    "create_package": "signature_only",
    "sead_descriptor_build": "callable",
    "sead_onmemory_bank_kind_lookup": "callable",
    "sead_onmemory_bank_release": "callable",
}
OLDER_RVAS = {
    "native_audio_updater_return": "0x010a5fc6", "native_audio_updater": "0x010a6020",
    "package_store_owner_global": "0x09243b30", "package_store_generation": "0x09187048",
    "package_store_owner_vtable": "0x05ec3468", "package_store_owner_interface_vtable": "0x05ec3498",
    "package_store_subscription_vtable": "0x05794ff8", "package_store_mount_callback": "0x0475b514",
    "package_store_factory": "0x047595ac", "package_store_shutdown": "0x047b0e20",
    "package_store_destructor": "0x047556f8", "dataobject_manager_lookup": "0x008bd0c0",
    "dataobject_manager_append": "0x01705fa0", "dataobject_manager_remove": "0x0170628c",
    "dataobject_manager_reset": "0x01706d28", "dataobject_name_helper": "0x0170602c",
    "streamable_manager_vtable": "0x06bfa8e8", "dataobject_name_map": "0x0939bd78",
    "bgmlist_object_vtable": "0x068afce0", "scoreinfo_object_vtable": "0x0691c2a0",
    "pianoscore_object_vtable": "0x0690f808", "scoreinfo_backing_vtable": "0x0691bf30",
    "scoreinfo_interface_vtable": "0x068fbdf0", "pianoscore_backing_vtable": "0x0690d718",
    "pianoscore_interface_vtable": "0x068b8538", "scoreinfo_typed_lookup": "0x043b6bb0",
    "pianoscore_typed_lookup": "0x041a06f8", "io_dispatcher_mount": "0x046c70b8",
    "package_store_full_initialize": "0x0475a300", "package_store_enclosing_request": "0x009af2b8",
    "single_soft_path_request": "0x009b1740", "bgmlist_append_bridge": "0x0415bf0c",
    "scoreinfo_append_bridge": "0x044b74d0", "pianoscore_append_bridge": "0x044ab5b8",
    "dataobject_group_reserve": "0x007e2988", "bgmlist_accessor": "0x0134904c",
    "pianoscore_accessor": "0x03c30524", "io_dispatcher_global": "0x0923a150",
    "streamable_service_global": "0x090fd850", "bgmlist_registry": "0x09226cc0",
    "scoreinfo_registry": "0x0922b450", "pianoscore_registry": "0x0922b400",
}
OLDER_CACHED_RVAS = dict(OLDER_RVAS)
OLDER_RVAS.update(OLDER_BOOTSTRAP_RVAS)


def expected_builds(identifier):
    return {BUILD, OLDER_BUILD} if identifier in OLDER_RVAS else {BUILD}

# id, C++ symbol, RVA, exact entry bytes, semantic contract.
FUNCTIONS = (
    ("io_dispatcher_mount", "IoDispatcherMount", "0x04440120",
     "4053b830000000e854bad2fd482be0488b442460488bda488b49084889442420e80b000000488bc34883c4305bc3",
     "RCX dispatcher; RDX caller-owned status; R8 environment; R9 encryption ID; stack argument encryption key. RAX equals status output, not success; status u32+0 is result."),
    ("package_store_full_initialize", "PackageStoreFullInitialize", "0x044d4d90",
     "48895c241057b840000000e8e06dc9fd482be0",
     "Historical cold-probe boundary; owner+648 dispatcher and owner+680 embedded PackageStore. Cold mount occurs at original full initializer return, caller return RVA 0x01996142. Nonzero subscription is copied from borrowed entry RSP+8 on that return, NOT a persistent owner field or lease. That probe alone did not qualify retained subscription/owner lifetime or later updater mounting. Never retain that stack address, replay initialization or infer late-mount authority from equal dispatcher pointers."),
    ("package_store_enclosing_request", "PackageStoreEnclosingRequest", "0x019960f8",
     "488bc44889581848895010488948085556574154415541564157488d68c1b8d0000000e8605a7d00482be0",
     "Enclosing request return boundary only; initializer owner+8 is request receiver. Preserve original output and result; no guessed direct invocation."),
    ("single_soft_path_request", "SingleSoftPathRequest", "0x011f2484",
     "488bc44889580848897010488978185541564157488d68c1b890000000e8da96f700482be0",
     "Eight arguments: manager, independent output pair, 24-byte soft path, delegate, int priority, zero, zero, debug string. Order follows music_load_probe.js acquire call, not the layout's stale unused_zero_arguments annotation. RAX equals output pair, not handle. Pair is handle/control; retain independent owning reference through process exit. Completion may be pending and alone is not acceptance."),
    ("bgmlist_append_bridge", "BgmListAppendBridge", "0x03ec9454",
     "48895c2408488974241057b830000000e817272afe482be0488bf9498bd8488b0d27f13005408af2",
     "Three arguments: authenticated class object, uint8 group, uint64 packed association. Additive group 2 only after capacity reservation and exclusive admission; native result is not a success contract."),
    ("scoreinfo_append_bridge", "ScoreInfoAppendBridge", "0x04227c64",
     "48895c2408488974241057b830000000e8073ff4fd482be0",
     "Three arguments: authenticated PianoScoreInfo object, uint8 group, uint64 packed association. Class-specific additive group 2 only; not interchangeable with BGMList or PianoScore bridge. No success return contract."),
    ("pianoscore_append_bridge", "PianoScoreAppendBridge", "0x0421bc98",
     "48895c2408488974241057b830000000e8d3fef4fd482be0",
     "Three arguments: authenticated PianoScore object, uint8 group, uint64 packed association. Class-specific additive group 2 only; not interchangeable with other bridges. No success return contract."),
    ("dataobject_group_reserve", "DataObjectGroupReserve", "0x007b3514",
     "48895c2408488974241057b820000000e857869b01482be0486379088d50e4",
     "One argument: exact group array header. Verify unchanged count and spare capacity before append; no success return contract. Native OOM is not recoverable or rollback-safe."),
    ("bgmlist_accessor", "BgmListAccessor", "0x00c92750",
     "4053b820000000e824944d01482be033c0488bd948894108884118488944243048894110488d052da69704488901e809000000488bc34883c4205bc3",
     "Exactly two semantic arguments: RCX writable zeroed 32-byte accessor, RDX uint64 packed FName by value. RAX equals output including miss; row+8, association+16, opaque backing byte+24; require nonnull row. Borrowed facts do not retain provider."),
    ("pianoscore_accessor", "PianoScoreAccessor", "0x039863d8",
     "4053b820000000e89c577efe482be033c0488bd948894108884118488944243048894110488d05eda3ca02488901e875818400488bc34883c4205bc3",
     "Exactly two semantic arguments: RCX writable zeroed 32-byte accessor, RDX uint64 packed FName by value. RAX equals output including miss; no semantic R8/R9 inputs. Require nonnull row; no PianoScore row-field reads qualified. Forwarder is not this ABI."),
)

GLOBALS = (
    ("io_dispatcher_global", "IoDispatcherGlobal", "0x090724d0", "Dispatcher pointer global; implementation at dispatcher+8; observation is not a lease."),
    ("streamable_service_global", "StreamableServiceGlobal", "0x08f30f60", "Service pointer global; embedded streamable manager+88, expected vtable RVA 0x06a60a00; validate service lifetime before use."),
    ("bgmlist_registry", "BgmListRegistry", "0x0905ee90", "Direct BGMList registry manager, not a pointer global; three 16-byte group headers."),
    ("scoreinfo_registry", "ScoreInfoRegistry", "0x090635d0", "Direct PianoScoreInfo registry manager; not interchangeable with PianoScore manager."),
    ("pianoscore_registry", "PianoScoreRegistry", "0x09063580", "Direct PianoScore registry manager; group membership is not provider retention or exclusive mutation authority."),
)

SCOREINFO_CONSTRUCTOR = bytes.fromhex(
    "4053b820000000e8189e81fe482be033c0488bd948894108884118488944243048894110488d05b977cd02488901e8b9d98c00488bc34883c4205bc3")


class NativeCachedResourceCatalogTest(unittest.TestCase):
    def test_runtime_gate_has_one_exact_build_owner_per_entrance(self):
        catalog = json.loads((ROOT / "src/game/rva_catalog.json").read_text())
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        hooks = [
            ("package_store_factory", "0x044d3d4c"),
            ("package_store_shutdown", "0x04539fdc"),
            ("package_store_destructor", "0x044cfc98"),
            ("package_store_full_initialize", "0x044d4d90"),
            ("dataobject_manager_lookup", "0x0076a330"),
            ("dataobject_manager_append", "0x01748fb8"),
            ("dataobject_manager_remove", "0x01748d70"),
            ("dataobject_manager_reset", "0x01749ba8"),
            ("dataobject_name_helper", "0x01749044"),
            ("dataobject_group_reserve", "0x007b3514"),
            ("native_audio_updater", "0x0136fc34"),
            ("native_audio_exe_entry", "0x0216c0a4"),
        ]
        self.assertEqual({entry["id"] for entry in entries.values()
                          if entry["hook_owner"] == "native_audio_runtime"},
                         {identifier for identifier, _ in hooks})
        for order, (identifier, rva) in enumerate(hooks, 30):
            with self.subTest(identifier=identifier):
                entry = entries[identifier]
                self.assertEqual(set(entry["builds"]), expected_builds(identifier))
                self.assertEqual(entry["builds"][BUILD]["rva"], rva)
                self.assertEqual(entry["install_policy"], "optional_hook")
                self.assertEqual(entry["hook_spec"], {"name": identifier, "order": order,
                                                       "required_for_release_startup": False})
                self.assertIn("src/game/native_audio_runtime.cpp", entry["consumers"])
        persistent = {
            "package_store_owner_global": "0x0907beb0", "package_store_generation": "0x091d9170",
            "package_store_owner_vtable": "0x05d232e0", "package_store_owner_interface_vtable": "0x05d23310",
            "package_store_subscription_vtable": "0x057a17b0",
            "native_audio_crt_state": "0x06fe2040",
            "native_audio_initial_gu_header": "0x0871eb28",
        }
        for identifier, rva in persistent.items():
            entry = entries[identifier]
            self.assertEqual(set(entry["builds"]), expected_builds(identifier))
            self.assertEqual(entry["builds"][BUILD]["rva"], rva)
            self.assertEqual(entry["install_policy"], "data_only")
            self.assertIsNone(entry["builds"][BUILD]["signature"])
        self.assertEqual(entries["native_audio_updater_return"]["builds"][BUILD]["signature"]["bytes"],
                          "c5 f8 28 74 24 20 48 83 c4 30 5b e9 02 00 00 00")
        self.assertEqual(entries["native_audio_exe_entry"]["builds"][BUILD]["signature"],
                         {"bytes": "48 83 ec 28 e8 bf 06 00 00 48 83 c4 28 e9 7a fe ff ff", "mask": "x" * 18})

    def test_readonly_binding_address_profiles_keep_policies(self):
        catalog = json.loads((ROOT / "src/game/rva_catalog.json").read_text())
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        profiles = {
            "dataobject_name_map": "0x091d85a0",
            "streamable_manager_vtable": "0x06a60a00",
            "bgmlist_object_vtable": "0x0673b9f8",
            "scoreinfo_object_vtable": "0x067a4eb8",
            "pianoscore_object_vtable": "0x067973a8",
            "scoreinfo_backing_vtable": "0x067a47f0",
            "scoreinfo_interface_vtable": "0x067845e0",
            "pianoscore_backing_vtable": "0x06795ea0",
            "pianoscore_interface_vtable": "0x067448a0",
            "scoreinfo_typed_lookup": "0x0412670c",
            "pianoscore_typed_lookup": "0x03f0deac",
        }
        for identifier, rva in profiles.items():
            with self.subTest(identifier=identifier):
                entry = entries[identifier]
                self.assertEqual(set(entry["builds"]), expected_builds(identifier))
                self.assertEqual(entry["builds"][BUILD]["rva"], rva)
                self.assertEqual(entry["requirement"], "research")
                self.assertEqual(entry["status"], "research")
                self.assertIsNone(entry["hook_spec"])
                self.assertIsNone(entry["hook_owner"])
                self.assertIn("src/game/native_audio_bindings.cpp", entry["consumers"])
                self.assertEqual(entry["install_policy"],
                                 "signature_only" if identifier.endswith("typed_lookup") else "data_only")

    def test_exact_build_contracts_and_single_owners(self):
        catalog = json.loads((ROOT / "src/game/rva_catalog.json").read_text())
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        for identifier, symbol, rva, expected, contract in FUNCTIONS:
            with self.subTest(identifier=identifier):
                entry = entries[identifier]
                self.assertEqual(entry["cpp_symbol"], symbol)
                self.assertEqual(set(entry["builds"]), expected_builds(identifier))
                self.assertEqual(entry["builds"][BUILD]["rva"], rva)
                self.assertEqual(entry["builds"][BUILD]["signature"],
                                 {"bytes": bytes.fromhex(expected).hex(" "), "mask": "x" * (len(expected) // 2)})
                owned = identifier in {"package_store_full_initialize", "dataobject_group_reserve"}
                policy = "optional_hook" if owned else "signature_only" if identifier == "package_store_enclosing_request" else "callable"
                self.assertEqual(entry["install_policy"], policy)
                self.assertEqual(entry["requirement"], "research")
                if owned:
                    self.assertEqual(entry["hook_owner"], "native_audio_runtime")
                    self.assertFalse(entry["hook_spec"]["required_for_release_startup"])
                else:
                    self.assertIsNone(entry["hook_spec"])
                    self.assertIsNone(entry["hook_owner"])
                self.assertIn(contract, entry["builds"][BUILD]["evidence"][0]["detail"])
        for identifier, symbol, rva, contract in GLOBALS:
            with self.subTest(identifier=identifier):
                entry = entries[identifier]
                self.assertEqual(entry["cpp_symbol"], symbol)
                self.assertEqual(set(entry["builds"]), expected_builds(identifier))
                self.assertEqual(entry["builds"][BUILD]["rva"], rva)
                self.assertEqual(entry["install_policy"], "data_only")
                self.assertIsNone(entry["builds"][BUILD]["signature"])
                self.assertIn(contract, entry["builds"][BUILD]["evidence"][0]["detail"])
        # The already hooked resolver and existing FName constructor keep their owners.
        self.assertEqual(entries["scoreinfo_resolver"]["builds"][BUILD]["rva"], "0x03951d5c")
        self.assertEqual(entries["scoreinfo_resolver"]["install_policy"], "required_hook")
        self.assertEqual(entries["scoreinfo_resolver"]["hook_owner"], "scoreinfo_overlay")
        scoreinfo = entries["scoreinfo_resolver"]["builds"][BUILD]
        self.assertTrue(SCOREINFO_CONSTRUCTOR.startswith(bytes.fromhex(scoreinfo["signature"]["bytes"])))
        self.assertIn("exactly two semantic arguments", scoreinfo["evidence"][0]["detail"])
        self.assertEqual(entries["fname_ctor"]["builds"][BUILD]["rva"], "0x00b517c4")

    def test_reviewed_older_addresses_and_imported_signatures(self):
        catalog = json.loads((ROOT / "src/game/rva_catalog.json").read_text())
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        retained = json.loads((ROOT / OLDER_EVIDENCE).read_text())
        self.assertEqual(retained["target_build"]["id"], OLDER_BUILD)
        self.assertEqual(retained["target_program"]["selector"], "ff7rebirth_.exe-1.004")
        self.assertTrue(retained["identity_evidence"]["timestamp_image_size_checksum_match"])
        self.assertFalse(retained["identity_evidence"]["whole_import_hash_available"])
        self.assertFalse(retained["identity_evidence"]["older_disk_binary_located"])
        self.assertEqual(len(OLDER_CACHED_RVAS), 42)
        self.assertEqual(set(retained["address_candidates"]), set(OLDER_CACHED_RVAS))
        signatures = dict(retained["byte_evidence"])
        # Supplementary imported read of the reviewed updater's normal caller return.
        signatures["native_audio_updater_return"] = "c5f8287424204883c4305be902000000"
        self.assertEqual(len(signatures), 23)
        for identifier, rva in OLDER_CACHED_RVAS.items():
            with self.subTest(identifier=identifier):
                self.assertEqual(retained["address_candidates"][identifier]["rva"], rva)
                old = entries[identifier]["builds"][OLDER_BUILD]
                self.assertEqual(old["rva"], rva)
                if identifier in signatures:
                    raw = bytes.fromhex(signatures[identifier])
                    self.assertEqual(old["signature"], {"bytes": raw.hex(" "), "mask": "x" * len(raw)})
                else:
                    self.assertIsNone(old["signature"])
                self.assertEqual(old["evidence"][0]["type"], "static_import_correlated")
                self.assertEqual(old["evidence"][0]["path"], OLDER_EVIDENCE)
        unmapped = {entry["id"] for entry in entries.values()
                    if entry["subsystem"] == "native_cached_resource" and OLDER_BUILD not in entry["builds"]}
        self.assertEqual(unmapped, UNMAPPED_OLDER)

    def test_supported_native_backend_still_requires_private_startup_scope(self):
        runtime = (ROOT / "src/game/native_audio_runtime.cpp").read_text()
        self.assertIn('const bool proxy_entry=native_proxy_detail::scope_present();', runtime)
        self.assertIn('if(synchronous==proxy_entry)return false;', runtime)
        self.assertIn('!native_audio_layout(core::generated::kBuildId)', runtime)
        self.assertIn('bootstrap_detail::begin_publication(module)', runtime)
        self.assertIn('bootstrap_detail::finish_publication(module)', runtime)
        self.assertIn('native_proxy_detail::begin_publication(module)', runtime)
        self.assertIn('native_proxy_detail::finish_publication(module,', runtime)

    def test_embedded_decoder_default_dependencies(self):
        entries = {entry["id"]: entry for entry in
                   json.loads((ROOT / "src/game/rva_catalog.json").read_text())["addresses"]}
        evidence_path = "analysis/NativeAudioGameDecoder-20261017.json"
        profile = json.loads((ROOT / evidence_path).read_text())["default_dependency_catalog_contract"]
        expected = {
            "native_oodle_default_allocate": ("NativeOodleDefaultAllocate", "0x01cc17c0", "0x01c04b00"),
            "native_oodle_default_free": ("NativeOodleDefaultFree", "0x01cc1820", "0x01c04b60"),
            "native_oodle_default_diagnostic": ("NativeOodleDefaultDiagnostic", "0x01cc1860", "0x01c04ba0"),
            "native_oodle_allocate_slot": ("NativeOodleAllocateSlot", "0x06c91be0", "0x06e5ebe0"),
            "native_oodle_free_slot": ("NativeOodleFreeSlot", "0x06c91be8", "0x06e5ebe8"),
            "native_oodle_diagnostic_slot": ("NativeOodleDiagnosticSlot", "0x06c91bf8", "0x06e5ebf8"),
            "native_oodle_profile_begin_slot": ("NativeOodleProfileBeginSlot", "0x06fa3fa8", "0x07170878"),
            "native_oodle_profile_end_slot": ("NativeOodleProfileEndSlot", "0x06fa3fb0", "0x07170880"),
            "native_oodle_stack_threshold": ("NativeOodleStackThreshold", "0x06c91ca8", "0x06e5eca8"),
        }
        self.assertEqual(set(profile["functions"]) | set(profile["data"]), set(expected))
        for identifier, (symbol, current_rva, older_rva) in expected.items():
            with self.subTest(identifier=identifier):
                entry = entries[identifier]
                function = identifier in profile["functions"]
                facts = profile["functions" if function else "data"][identifier]
                self.assertEqual(entry["cpp_symbol"], symbol)
                self.assertEqual(entry["consumers"], ["src/game/native_audio_decoder.cpp"])
                self.assertEqual(entry["kind"], "function" if function else "global")
                self.assertEqual(entry["install_policy"], "callable" if function else "data_only")
                self.assertEqual(entry["validation"]["policy"], "signature" if function else "data_reference")
                self.assertEqual(entry["status"], "research")
                self.assertEqual(entry["requirement"], "research")
                self.assertIsNone(entry["hook_owner"])
                self.assertIsNone(entry["hook_spec"])
                self.assertEqual(set(entry["builds"]), {BUILD, OLDER_BUILD})
                for build, rva, key, kind in (
                    (BUILD, current_rva, "current", "static_disk_contract"),
                    (OLDER_BUILD, older_rva, "older", "static_import_contract"),
                ):
                    value = entry["builds"][build]
                    self.assertEqual(value["rva"], rva)
                    self.assertEqual(facts[key + "_rva"], rva)
                    self.assertEqual(value["evidence"][0]["type"], kind)
                    self.assertEqual(value["evidence"][0]["path"], evidence_path)
                    if function:
                        raw = bytes.fromhex(facts[key + "_signature"])
                        self.assertEqual(value["signature"], {"bytes": raw.hex(" "), "mask": "x" * len(raw)})
                    else:
                        self.assertIsNone(value["signature"])
        for stem in ("allocate", "free", "diagnostic"):
            slot = profile["data"]["native_oodle_" + stem + "_slot"]
            self.assertEqual(slot["width"], 8)
            self.assertEqual(slot["expected_target_id"], "native_oodle_default_" + stem)
        for identifier in ("native_oodle_profile_begin_slot", "native_oodle_profile_end_slot"):
            self.assertEqual(profile["data"][identifier]["width"], 8)
            self.assertEqual(profile["data"][identifier]["expected_value"], 0)
        self.assertEqual(profile["data"]["native_oodle_stack_threshold"]["width"], 4)
        self.assertEqual(profile["data"]["native_oodle_stack_threshold"]["expected_value"], 32768)

    def test_removed_audio_hooks_keep_evidence_without_runtime_ownership(self):
        catalog = json.loads((ROOT / "src/game/rva_catalog.json").read_text())
        entries = {entry["id"]: entry for entry in catalog["addresses"]}
        self.assertFalse(any(entry["hook_owner"] == "audio_sead" for entry in entries.values()))
        for identifier, policy in RETIRED_AUDIO_BINDINGS.items():
            with self.subTest(identifier=identifier):
                entry = entries[identifier]
                consumers = ["tools/test_native_cached_resource_catalog.py"]
                if identifier == "static_construct_object":
                    consumers.append("src/game/menu_theme_native.cpp")
                self.assertEqual(entry["consumers"], consumers)
                self.assertEqual(entry["install_policy"], policy)
                self.assertEqual(entry["requirement"], "research")
                self.assertEqual(entry["status"], "research")
                self.assertIsNone(entry["hook_spec"])
                self.assertIsNone(entry["hook_owner"])
                for facts in entry["builds"].values():
                    self.assertGreater(int(facts["rva"], 16), 0)
                    self.assertTrue(facts["evidence"])
                    self.assertEqual(facts["signature"] is None, policy == "data_only"
                                     or identifier == "sead_descriptor_build")
        # Wiring still has a direct native Set reference, not an installed hook.
        native_set = entries["bgm_slot_set"]
        self.assertEqual(native_set["consumers"], ["src/game/audio_production_wiring.cpp"])
        self.assertEqual(native_set["install_policy"], "callable")
        self.assertIsNone(native_set["hook_spec"])
        self.assertIsNone(native_set["hook_owner"])
        self.assertEqual({entry["id"] for entry in entries.values()
                          if "src/game/audio_sead.cpp" in entry["consumers"]},
                         {"persistent_chart_expand_caller"})


if __name__ == "__main__":
    unittest.main()
