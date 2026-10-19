import argparse
import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
import wave

import vanilla_piano_benchmark as benchmark


def fixture():
    mono = [{"RowName": name, "SoundName": "Cn4", "HandType": 0, "KeyAssign": key,
             "Semitone": 0, "OctaveInt": 1, "SocketName": "key"}
            for name, key in (("Cn4", 0), ("Cn4_2", 7))]
    chord = {"RowName": "pca_C", "ChordID": "C", "PageIndex": 2, "KeyAssign": 0,
             "Semitone": 0, "Minor": 0, "SocketName": "chord"}
    score = {"RowName": "Tiny", "TimeStr_Array": ["0_00", "0_06", "0_12", "0_59"],
             "MonotoneID_Array": ["Cn4", "Cn4_2", "", "Cn4"],
             "ChordID_Array": ["pca_C", "", "pca_C"], "GroupIndex_Array": [1, 1],
             "IgnoreSound_Array": ["En2", "", "", "", "", "", "Cn2", "En2", "Gn2"],
             "MonotoneNoteType_Array": [2], "MonotoneDotType_Array": [1], "Strength_Array": []}
    return [{"table": name, "rows": rows} for name, rows in (
        ("PianoScore", [score]),
        ("PianoScoreInfo", [{"RowName": "Tiny", "Difficulty": 1, "Score_Array": [0, 400], "Bpm": 0, "StreamingFrame": 60}]),
        ("PianoMonotoneAssign", mono), ("PianoChordsAssign", [chord]),
        ("PianoChordsConfig", [{"RowName": "C", "SoundName_Array": ["Cn2", "En2", "Gn2"]}]),
        ("PianoSound", [{"RowName": name} for name in ("Cn2", "En2", "Gn2", "Cn4", "Dn2")]))]


def read_exported_midi(data):
    """Independent minimal SMF reader for this tool's explicit-status output."""
    assert data[:4] == b"MThd"
    size, form, count, division = struct.unpack_from(">IHHH", data, 4)
    assert (size, form, division) == (6, 1, 60)
    pos, onsets, note_offs, tempos = 14, [], [], []
    for _ in range(count):
        assert data[pos:pos + 4] == b"MTrk"
        length = struct.unpack_from(">I", data, pos + 4)[0]
        pos += 8
        end, tick = pos + length, 0
        while pos < end:
            delta = 0
            while True:
                value = data[pos]
                pos += 1
                delta = delta * 128 + (value & 127)
                if value < 128:
                    break
            tick += delta
            status = data[pos]
            pos += 1
            if status == 255:
                kind, length = data[pos:pos + 2]
                pos += 2
                if kind == 81:
                    tempos.append(int.from_bytes(data[pos:pos + length], "big"))
                pos += length
            else:
                pitch, velocity = data[pos:pos + 2]
                pos += 2
                assert status & 240 in (128, 144)
                (onsets if status & 240 == 144 else note_offs).append((tick, status & 15, pitch, velocity))
        assert pos == end
    assert pos == len(data) and tempos == [1000000]
    return onsets, note_offs


class VanillaBenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.decoded = benchmark.tables(fixture())
        self.assignments = benchmark.Assignments(self.decoded)
        self.rows = benchmark.stock_rows(self.decoded["PianoScore"]["Tiny"], self.assignments)

    def test_partial_arrays_three_ignore_slots_and_silent_chord(self):
        self.assertEqual([row["group_index"] for row in self.rows], [1, 1, 0, 0])
        self.assertEqual(self.rows[0]["events"][1]["pitches"], [36, 43])
        self.assertEqual(self.rows[2]["events"][0]["pitches"], [])
        self.assertTrue(self.rows[2]["events"][0]["required"])
        self.assertEqual(self.rows[3]["source"]["ignore_sound_slots"], ["", "", ""])
        self.assertEqual(self.rows[3]["source"]["ChordID_Array"], "")
        partial = dict(self.decoded["PianoScore"]["Tiny"], IgnoreSound_Array=["En2"])
        rows = benchmark.stock_rows(partial, self.assignments)
        self.assertEqual(rows[0]["source"]["ignore_sound_slots"], ["En2", "", ""])
        self.assertEqual(rows[0]["events"][1]["pitches"], [36, 43])

    def test_group_follower_crosses_hands_and_group_reuse_resets(self):
        rows = [{"frame": i, "group_index": group, "events": [self.assignments.event(hand, identity)]}
                for i, (group, hand, identity) in enumerate(((1, "right", "Cn4"), (1, "left", "pca_C"),
                                                            (0, "right", "Cn4"), (1, "right", "Cn4")))]
        benchmark.plan_roots(rows)
        self.assertEqual([row["events"][0]["required"] for row in rows], [True, False, True, True])
        self.assertEqual(rows[1]["group_root_row"], 0)

    def test_exact_smf_frame_ticks_and_retrigger_gates(self):
        onsets, offs = read_exported_midi(benchmark.smf(self.rows, 12, 75))
        self.assertEqual({(tick, pitch) for tick, _, pitch, _ in onsets}, benchmark.music(self.rows))
        self.assertIn((59, 0, 60, 75), onsets)
        self.assertIn((6, 0, 60, 0), offs)
        self.assertIn((18, 0, 60, 0), offs)
        self.assertFalse(any(tick == 12 for tick, _, _, _ in onsets))

    def test_independent_root_and_music_retention_and_raw_controls(self):
        original = benchmark.metrics(self.rows, 20)
        self.assertEqual(original["required_inputs"], 4)
        self.assertEqual(original["hands"]["right"]["followers"], 1)
        self.assertEqual(original["hands"]["right"]["gap_frames"], [59])
        self.assertEqual(len(original["hands"]["right"]["series"]), 2)
        self.assertEqual(original["hands"]["right"]["control_changes"], 0)
        regrouped = json.loads(json.dumps(self.rows))
        regrouped[1]["group_index"] = 0
        benchmark.plan_roots(regrouped)
        result = benchmark.retention(self.rows, regrouped)
        self.assertEqual(result["musical_onset_pitch"]["recall"], 1)
        self.assertEqual(benchmark.metrics(regrouped, 60)["required_inputs"], 5)
        self.assertEqual(benchmark.metrics(regrouped, 60)["hands"]["right"]["control_changes"], 2)
        regrouped[0]["events"][0]["assignment_id"] = "Cn4_2"
        result = benchmark.retention(self.rows, regrouped)
        self.assertEqual(result["musical_onset_pitch"]["recall"], 1)
        self.assertEqual(result["original_required_roots"]["recall"], .75)
        self.assertEqual(result["original_required_roots"]["generated"], 5)
        self.assertEqual(result["original_required_roots"]["precision"], .6)
        self.assertEqual(result["musical_onset_pitch"]["missing"], [])
        self.assertEqual(result["original_required_roots"]["missing"],
                         [{"frame": 0, "hand": "right", "assignment_id": "Cn4", "count": 1}])

    def test_joint_required_frames_and_control_changes_exclude_followers(self):
        rows = []
        for frame, (group, right_changed, left_changed, left_present) in enumerate((
                (1, False, False, True), (1, True, True, True),
                (0, False, False, True), (0, True, True, True),
                (0, True, True, True), (0, False, False, False),
                (0, False, False, True))):
            events = [self.assignments.event("right", "Cn4_2" if right_changed else "Cn4")]
            if left_present:
                left = self.assignments.event("left", "pca_C")
                if left_changed:
                    left["assignment_id"] = "pca_changed"
                    left["control"]["KeyAssign"] = 1
                events.append(left)
            rows.append({"frame": frame, "group_index": group, "events": events})
        result = benchmark.metrics(benchmark.plan_roots(rows), 20)
        self.assertEqual(result["required_inputs"], 11)
        self.assertEqual(result["hands"]["right"]["followers"], 1)
        self.assertEqual(result["hands"]["left"]["followers"], 1)
        self.assertEqual(result["joint"]["both_hands_required"],
                         {"count": 5, "frames": [0, 2, 3, 4, 6]})
        self.assertEqual(result["joint"]["both_hands_control_changes"],
                         {"count": 1, "frames": [3]})

    def test_missing_records_are_sorted_and_match_aggregate_losses(self):
        original = self.rows + [self.rows[0]]  # Two identical original roots at frame zero.
        regenerated = json.loads(json.dumps([self.rows[0], self.rows[2]]))
        regenerated[0]["events"][0]["assignment_id"] = "Cn4_2"
        regenerated[0]["events"][1]["pitches"] = [36]
        result = benchmark.retention(original, regenerated)
        self.assertEqual(result, benchmark.retention(list(reversed(original)), regenerated))
        music, roots = result["musical_onset_pitch"], result["original_required_roots"]
        self.assertEqual(music["missing"], [{"frame": 0, "pitch": 43},
                                            {"frame": 6, "pitch": 60},
                                            {"frame": 59, "pitch": 60}])
        self.assertEqual(roots["missing"], [
            {"frame": 0, "hand": "left", "assignment_id": "pca_C", "count": 1},
            {"frame": 0, "hand": "right", "assignment_id": "Cn4", "count": 2},
            {"frame": 59, "hand": "right", "assignment_id": "Cn4", "count": 1}])
        self.assertEqual(len(music["missing"]), music["expected"] - music["matched"])
        self.assertEqual(sum(item["count"] for item in roots["missing"]),
                          roots["expected"] - roots["matched"])
        self.assertEqual(sum(item["count"] for item in roots["additional"]),
                         roots["generated"] - roots["matched"])
        self.assertEqual(roots["precision"], roots["matched"] / roots["generated"])

    def test_unknown_mappings_fail_and_known_unmatched_ignore_is_visible(self):
        with self.assertRaises(KeyError):
            self.assignments.event("left", "pca_unknown")
        with self.assertRaises(ValueError):
            self.assignments.event("left", "pca_C", ["Unknown"])
        event = self.assignments.event("left", "pca_C", ["Dn2"])
        self.assertEqual(event["unmatched_ignore_sound"], ["Dn2"])
        with self.assertRaises(ValueError):
            benchmark.resolved_rows({"bpm": 60}, [{"beat": 0, "chord_id": "pca_C", "ignore_sound": ["Dn2"]}], self.assignments)
        for value in ("0_60", "-1_00", "not-a-time"):
            with self.assertRaises(ValueError):
                benchmark.native_frame(value)

    def test_export_control_compare_and_refuse_existing_folder(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()):
            root = Path(temporary)
            corpus = root / "corpus.json"
            benchmark.write_json(corpus, fixture())
            args = argparse.Namespace(corpus=corpus, assignments=corpus, out=root / "export",
                                      gate_frames=12, series_gap_frames=60, velocity=75)
            benchmark.export(args)
            folder = args.out / "Tiny"
            control = benchmark.load(folder / "original-chart.song.json")
            self.assertEqual(control["notes"][0]["monotone_note_value"], "dotted_quarter")
            self.assertEqual(control["notes"][1]["monotone_variant"], "alternate")
            self.assertEqual(control["notes"][3]["monotone_note_value"], "whole")
            with wave.open(str(folder / "song.wav")) as audio:
                self.assertEqual(audio.getframerate(), 48000)
                self.assertEqual(audio.getnframes(), (59 + 12 + 120) * 800)
            compare = argparse.Namespace(baseline=folder / "baseline.json", assignments=corpus,
                                         resolved=folder / "original-chart.song.json", out=root / "report.json")
            benchmark.compare(compare)
            result = benchmark.load(compare.out)["profiles"][0]["retention"]
            self.assertEqual(result["musical_onset_pitch"]["recall"], 1)
            self.assertEqual(result["original_required_roots"]["recall"], 1)
            with self.assertRaisesRegex(ValueError, "must be empty"):
                benchmark.export(args)


if __name__ == "__main__":
    unittest.main()
