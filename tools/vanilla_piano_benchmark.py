#!/usr/bin/env python3
"""Offline stock-chart export and independent resolved-chart diagnostics (stdlib only)."""
import argparse
from array import array
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import wave

FPS = 60
NOTE_NAMES = ("whole", "half", "quarter", "eighth", "sixteenth", "one_third", "one_sixth")
MONO_FIELDS = ("HandType", "KeyAssign", "Semitone", "OctaveInt", "SocketName")
CHORD_FIELDS = ("PageIndex", "KeyAssign", "Semitone", "Minor", "SocketName")


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def index_rows(rows):
    indexed = {row["RowName"]: row for row in rows}
    if len(indexed) != len(rows):
        raise ValueError("duplicate RowName")
    return indexed


def tables(document):
    if not isinstance(document, list):
        raise ValueError("expected a list of decoded tables")
    indexed = {table["table"]: index_rows(table["rows"]) for table in document}
    if len(indexed) != len(document):
        raise ValueError("duplicate decoded table")
    return indexed


def sound_pitch(sound):
    match = re.fullmatch(r"([A-G])([nsb])(\d)", sound)
    if not match:
        raise ValueError(f"unknown native sound mapping: {sound!r}")
    letter, accidental, octave = match.groups()
    pitch = 12 * (int(octave) + 1) + dict(C=0, D=2, E=4, F=5, G=7, A=9, B=11)[letter]
    pitch += {"n": 0, "s": 1, "b": -1}[accidental]
    if not 0 <= pitch <= 127:
        raise ValueError(f"sound outside MIDI range: {sound}")
    return pitch


def author_pitch(sound):
    sound_pitch(sound)
    return sound[0] + {"n": "", "s": "#", "b": "b"}[sound[1]] + sound[2:]


class Assignments:
    def __init__(self, decoded):
        self.mono = decoded["PianoMonotoneAssign"]
        self.chords = decoded["PianoChordsAssign"]
        self.config = decoded["PianoChordsConfig"]
        self.sounds = decoded["PianoSound"]

    def sound(self, name):
        if name not in self.sounds:
            raise ValueError(f"sound not in verified PianoSound: {name!r}")
        return sound_pitch(name)

    def event(self, hand, identity, ignored=()):
        source = (self.mono if hand == "right" else self.chords)[identity]
        if hand == "right":
            fields = MONO_FIELDS
            sounds = [source["SoundName"]]
        else:
            fields = CHORD_FIELDS
            sounds = self.config[source["ChordID"]]["SoundName_Array"]
            if not sounds:
                raise ValueError(f"empty chord composition: {identity}")
        if len(ignored) > 3:
            raise ValueError(f"too many IgnoreSound slots for {identity}")
        for name in ignored:
            self.sound(name)
        return {"hand": hand, "assignment_id": identity,
                "control": {field: source[field] for field in fields},
                "unmatched_ignore_sound": [name for name in ignored if name not in sounds],
                "pitches": [self.sound(sound) for sound in sounds if sound not in ignored]}

    def mono_id(self, pitch, variant="default"):
        match = re.fullmatch(r"([A-G])([#b]?)(\d)", pitch)
        if not match or variant not in ("default", "alternate"):
            raise ValueError(f"unknown authored pitch/variant: {pitch!r}/{variant!r}")
        letter, accidental, octave = match.groups()
        identity = letter + {"": "n", "#": "s", "b": "b"}[accidental] + octave
        if variant == "alternate":
            identity += "_2"
        if identity not in self.mono:
            raise ValueError(f"authored pitch has no verified assignment: {identity}")
        return identity


def native_frame(time):
    match = re.fullmatch(r"(\d+)_(\d{1,2})", time)
    if not match or int(match[2]) >= FPS:
        raise ValueError(f"invalid native frame time: {time!r}")
    return int(match[1]) * FPS + int(match[2])


def tail(values, index, default):
    return values[index] if index < len(values) else default


def plan_roots(rows):
    previous, root = 0, None
    for index, row in enumerate(rows):
        group = row["group_index"]
        if not isinstance(group, int) or not 0 <= group <= 255:
            raise ValueError(f"invalid group: {group}")
        if group == 0 or group != previous:
            root = None
        required = group == 0 or root is None
        if group and root is None and row["events"]:
            root = index
        row["group_root_row"] = root
        for event in row["events"]:
            event["required"] = required
        previous = group
    return rows


def stock_rows(score, assignments):
    times = score["TimeStr_Array"]
    for key, value in score.items():
        if isinstance(value, list) and len(value) > len(times) * (3 if key == "IgnoreSound_Array" else 1):
            raise ValueError(f"overlong source array: {key}")
    result = []
    for index, time in enumerate(times):
        source = {key: tail(value, index, "" if key in ("MonotoneID_Array", "ChordID_Array") else 0)
                  for key, value in score.items() if isinstance(value, list) and key not in ("TimeStr_Array", "IgnoreSound_Array")}
        ignored = [tail(score.get("IgnoreSound_Array", []), index * 3 + slot, "") for slot in range(3)]
        for name in ignored:
            if name:
                assignments.sound(name)
        events = []
        mono, chord = source.get("MonotoneID_Array", ""), source.get("ChordID_Array", "")
        if mono:
            events.append(assignments.event("right", mono))
        if chord:
            events.append(assignments.event("left", chord, [name for name in ignored if name]))
        result.append({"frame": native_frame(time), "group_index": source.get("GroupIndex_Array", 0),
                       "events": events, "source": {"time": time, **source, "ignore_sound_slots": ignored}})
    if any(a["frame"] > b["frame"] for a, b in zip(result, result[1:])):
        raise ValueError("stock rows are not chronological")
    return plan_roots(result)


def resolved_rows(song, notes, assignments):
    if song.get("chord_voicings"):
        raise ValueError("custom chord voicings are outside this verified-stock benchmark")
    bpm = song["bpm"]
    if not math.isfinite(bpm) or not 30 <= bpm <= 300:
        raise ValueError("invalid resolved bpm")
    result = []
    for note in notes:
        beat = note["beat"]
        if not math.isfinite(beat) or beat < 0:
            raise ValueError("invalid resolved beat")
        events = []
        if "pitch" in note:
            events.append(assignments.event("right", assignments.mono_id(note["pitch"], note.get("monotone_variant", "default"))))
        if "chord_id" in note:
            event = assignments.event("left", note["chord_id"], note.get("ignore_sound", []))
            if event["unmatched_ignore_sound"]:
                raise ValueError("authored IgnoreSound is not a selected chord constituent")
            events.append(event)
        if not events:
            raise ValueError("resolved row has no supported event")
        result.append({"frame": math.floor(beat * 60 / bpm * FPS + 0.5),
                       "group_index": note.get("group_index", 0), "events": events})
    if any(a["frame"] > b["frame"] for a, b in zip(result, result[1:])):
        raise ValueError("resolved rows are not chronological")
    return plan_roots(result)


def quantile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[math.floor((len(ordered) - 1) * fraction)]


def metrics(rows, series_gap_frames):
    result = {"series_gap_frames": series_gap_frames, "rows": len(rows), "hands": {}}
    root_frames = {}
    total = sum(event["required"] for row in rows for event in row["events"])
    result["required_inputs"] = total
    for hand in ("right", "left"):
        events = [(row["frame"], event) for row in rows for event in row["events"] if event["hand"] == hand]
        roots = [(frame, event) for frame, event in events if event["required"]]
        root_frames[hand] = {frame for frame, _ in roots}
        gaps = [b[0] - a[0] for a, b in zip(roots, roots[1:])]
        series, current = [], []
        for item in roots:
            if current and item[0] - current[-1][0] > series_gap_frames:
                series.append(current)
                current = []
            current.append(item)
        if current:
            series.append(current)
        transitions = []
        for (start, before), (end, after) in zip(roots, roots[1:]):
            transitions.append({"from_frame": start, "to_frame": end,
                                "from": before["assignment_id"], "to": after["assignment_id"],
                                "changed_fields": [field for field in before["control"]
                                                   if before["control"][field] != after["control"][field]]})
        result["hands"][hand] = {
            "events": len(events), "required_inputs": len(roots), "followers": len(events) - len(roots),
            "required_share": len(roots) / total if total else None,
            "gap_frames": gaps, "gap_p50_frames": quantile(gaps, .5), "gap_p90_frames": quantile(gaps, .9),
            "minimum_gap_frames": min(gaps, default=None), "maximum_gap_frames": max(gaps, default=None),
            "series": [{"inputs": len(run), "start_frame": run[0][0], "end_frame": run[-1][0],
                        "span_frames": run[-1][0] - run[0][0]} for run in series],
            "assignment_transitions": transitions,
            "control_changes": sum(bool(item["changed_fields"]) for item in transitions)}
    change_frames = {hand: {item["to_frame"] for item in data["assignment_transitions"]
                            if item["changed_fields"]} for hand, data in result["hands"].items()}
    result["joint"] = {}
    for name, frames_by_hand in (("both_hands_required", root_frames),
                                 ("both_hands_control_changes", change_frames)):
        frames = sorted(frames_by_hand["right"] & frames_by_hand["left"])
        result["joint"][name] = {"count": len(frames), "frames": frames}
    return result


def music(rows):
    return {(row["frame"], pitch) for row in rows for event in row["events"] for pitch in event["pitches"]}


def roots(rows):
    return Counter((row["frame"], event["hand"], event["assignment_id"])
                   for row in rows for event in row["events"] if event["required"])


def retention(original, regenerated):
    expected, actual = music(original), music(regenerated)
    old_roots, new_roots = roots(original), roots(regenerated)
    matched = len(expected & actual)
    root_count = sum(old_roots.values())
    generated_roots = sum(new_roots.values())
    retained = sum((old_roots & new_roots).values())
    return {"musical_onset_pitch": {"expected": len(expected), "generated": len(actual), "matched": matched,
                                   "recall": matched / len(expected) if expected else None,
                                   "precision": matched / len(actual) if actual else None,
                                   "missing": [{"frame": frame, "pitch": pitch}
                                               for frame, pitch in sorted(expected - actual)]},
             "original_required_roots": {"expected": root_count, "generated": generated_roots, "matched": retained,
                                          "recall": retained / root_count if root_count else None,
                                          "precision": retained / generated_roots if generated_roots else None,
                                          "missing": [{"frame": frame, "hand": hand,
                                                       "assignment_id": identity, "count": count}
                                                      for (frame, hand, identity), count
                                                      in sorted((old_roots - new_roots).items())],
                                          "additional": [{"frame": frame, "hand": hand,
                                                          "assignment_id": identity, "count": count}
                                                         for (frame, hand, identity), count
                                                         in sorted((new_roots - old_roots).items())]}}


def vlq(value):
    if not 0 <= value <= 0x0fffffff:
        raise ValueError("MIDI delta exceeds SMF limit")
    output = [value & 127]
    while value >> 7:
        value >>= 7
        output.insert(0, (value & 127) | 128)
    return bytes(output)


def smf(rows, gate_frames, velocity):
    # 60 PPQ at exactly 60 BPM: every SMF tick is exactly one native frame.
    tracks = []
    for channel, hand in enumerate(("right", "left")):
        onsets = sorted({(row["frame"], pitch) for row in rows for event in row["events"]
                         if event["hand"] == hand for pitch in event["pitches"]})
        events = [(0, b"\xff\x51\x03\x0f\x42\x40")] if channel == 0 else []
        next_onset = {}
        for frame, pitch in reversed(onsets):
            end = min(frame + gate_frames, next_onset.get(pitch, frame + gate_frames))
            events.extend(((frame, bytes((0x90 | channel, pitch, velocity))),
                           (end, bytes((0x80 | channel, pitch, 0)))))
            next_onset[pitch] = frame
        events.sort(key=lambda item: (item[0], item[1][0]))  # Note-off before retrigger.
        data, previous = bytearray(), 0
        for frame, message in events:
            data.extend(vlq(frame - previous) + message)
            previous = frame
        data.extend(b"\x00\xff\x2f\x00")
        tracks.append(b"MTrk" + struct.pack(">I", len(data)) + data)
    return b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), FPS) + b"".join(tracks)


def neutral_audio(path, frames):
    # Deliberately unrelated to the notes: not stock audio or listening evidence.
    samples = array("h", (int(2000 * math.sin(2 * math.pi * sample / 48)) for sample in range(48000)))
    if sys.byteorder != "little":
        samples.byteswap()
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        remaining = frames * 800
        while remaining:
            count = min(remaining, 48000)
            output.writeframes(samples[:count].tobytes())
            remaining -= count


def notation(source, prefix):
    kind, dot = source.get(prefix + "NoteType_Array", 0), source.get(prefix + "DotType_Array", 0)
    if kind not in range(len(NOTE_NAMES)) or dot not in (0, 1):
        raise ValueError(f"unknown notation {kind}/{dot}")
    return ("dotted_" if dot else "") + NOTE_NAMES[kind]


def baseline_song(name, rows, assignments, gate_frames, difficulty):
    notes = []
    for row in rows:
        if not row["events"]:
            continue  # Empty source rows remain in the sidecar; v2 has no empty notes.
        note = {"beat": row["frame"] / FPS, "duration_beats": gate_frames / FPS}
        if row["group_index"]:
            note["group_index"] = row["group_index"]
        for event in row["events"]:
            identity = event["assignment_id"]
            if event["hand"] == "right":
                note["pitch"] = author_pitch(assignments.mono[identity]["SoundName"])
                if identity.endswith("_2"):
                    note["monotone_variant"] = "alternate"
                note["monotone_note_value"] = notation(row["source"], "Monotone")
            else:
                note["chord_id"] = identity
                note["chord_note_value"] = notation(row["source"], "Chord")
                ignored = [name for name in row["source"]["ignore_sound_slots"]
                           if name and name not in event["unmatched_ignore_sound"]]
                if ignored:
                    note["ignore_sound"] = ignored
        notes.append(note)
    return {"schema": "ff7rpianosongs.song.v2", "title": name + " stock chart control", "bpm": 60,
            "difficulty": difficulty, "notes": notes, "loudness_normalization": False}


def export(args):
    corpus, assignment_tables = tables(load(args.corpus)), tables(load(args.assignments))
    assignments = Assignments(assignment_tables)
    out = Path(args.out)
    if out.exists() and any(out.iterdir()):
        raise ValueError("export directory must be empty (never reuse a production cache)")
    out.mkdir(parents=True, exist_ok=True)
    summary = []
    for name, score in corpus["PianoScore"].items():
        if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
            raise ValueError(f"unsafe song folder name: {name}")
        info = corpus["PianoScoreInfo"][name]
        if info.get("StreamingFrame") != FPS:
            raise ValueError(f"{name}: expected verified 60-fps StreamingFrame")
        rows = stock_rows(score, assignments)
        measured = metrics(rows, args.series_gap_frames)
        maximum_score = max(info["Score_Array"])
        if maximum_score != 100 * measured["required_inputs"]:
            raise ValueError(f"{name}: required-root count disagrees with stock maximum score")
        song_dir = out / name
        song_dir.mkdir()
        assumptions = {"onsets": "exact source seconds_frame at 60 fps; SMF tick=frame",
                       "tempo": "60 BPM timing carrier, not a recovered musical tempo (stock Bpm may be zero)",
                       "note_off": f"assumed {args.gate_frames}-frame gate; same-pitch retrigger truncates gate",
                       "notation": "original native note/dot types preserved separately; not measured sustain",
                       "inert_ignore_sound": "source slots on rows without chords retained in sidecar, with no sounding effect",
                       "ignore_matching": "literal sound-name filtering; unmatched known names reported on events, not octave-folded",
                       "velocity": f"constant {args.velocity}; not a recovered performance velocity",
                       "audio": "neutral synthetic 1 kHz analysis signal; NOT stock audio or musical listening evidence",
                       "smf_duplicates": "coincident same-hand/same-pitch sounds merged; original events retained in sidecar",
                       "controls": "raw assignment field transitions only; no angle, neutral-return or human-limit semantics"}
        sidecar = {"schema": "ff7rpianosongs.vanilla-benchmark.v1", "song": name,
                   "source_info": info, "assumptions": assumptions, "rows": rows, "metrics": measured}
        write_json(song_dir / "baseline.json", sidecar)
        baseline = baseline_song(name, rows, assignments, args.gate_frames, info["Difficulty"])
        # Verify authored control faithfully retains all sounds and required identities.
        authored = resolved_rows(baseline, baseline["notes"], assignments)
        if music(rows) != music(authored) or roots(rows) != roots(authored):
            raise ValueError(f"{name}: authored baseline loses source events")
        write_json(song_dir / "original-chart.song.json", baseline)
        midi = smf(rows, args.gate_frames, args.velocity)
        (song_dir / "song.mid").write_bytes(midi)
        write_json(song_dir / "song.json", {"schema": "ff7rpianosongs.song.v2", "title": name + " stock onset roundtrip",
                   "bpm": 60, "midi_audio_offset_seconds": 0, "midi_minimum_lead_in_seconds": 0, "loudness_normalization": False})
        final_frame = max(row["frame"] for row in rows) + args.gate_frames + FPS * 2
        neutral_audio(song_dir / "song.wav", final_frame)
        summary.append({"song": name, "stock_difficulty": info["Difficulty"], "required_inputs": measured["required_inputs"],
                        "musical_onset_pitches": len(music(rows)), "midi_sha256": hashlib.sha256(midi).hexdigest()})
    write_json(out / "export.json", {"sources": {"corpus_sha256": hashlib.sha256(Path(args.corpus).read_bytes()).hexdigest(),
                                                 "assignments_sha256": hashlib.sha256(Path(args.assignments).read_bytes()).hexdigest()},
                                     "songs": summary})
    print(json.dumps(summary, indent=2))


def compare(args):
    baseline = load(args.baseline)
    assignments = Assignments(tables(load(args.assignments)))
    song = load(args.resolved)
    profiles = song.get("profiles", [{"difficulty": song.get("difficulty"), "notes": song.get("notes", [])}])
    result = {"song": baseline["song"], "assumptions": baseline["assumptions"], "baseline": baseline["metrics"],
              "matching": "exact compiled 60-fps onset + MIDI pitch; roots additionally require hand + assignment identity",
              "profiles": []}
    for profile in profiles:
        rows = resolved_rows(song, profile["notes"], assignments)
        result["profiles"].append({"difficulty": profile["difficulty"],
                                  "metrics": metrics(rows, baseline["metrics"]["series_gap_frames"]),
                                  "retention": retention(baseline["rows"], rows)})
    write_json(args.out, result)
    print(json.dumps({"song": result["song"], "profiles": [
        {"difficulty": p["difficulty"], "required_inputs": p["metrics"]["required_inputs"], **p["retention"]}
        for p in result["profiles"]]}, indent=2))


def positive(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    export_parser = commands.add_parser("export", help="export verified decoded corpus to isolated MIDI song folders")
    export_parser.add_argument("--corpus", required=True)
    export_parser.add_argument("--assignments", required=True, help="decoded assignment/config/sound tables; may be same JSON as corpus")
    export_parser.add_argument("--out", required=True)
    export_parser.add_argument("--gate-frames", type=positive, required=True, help="explicit assumed gate, NOT recovered sustain")
    export_parser.add_argument("--series-gap-frames", type=positive, required=True, help="explicit reporting cutoff, NOT a universal human limit")
    export_parser.add_argument("--velocity", type=positive, default=75)
    export_parser.set_defaults(run=export)
    compare_parser = commands.add_parser("compare", help="compare original sidecar with regenerated resolved-song.json")
    for option in ("baseline", "assignments", "resolved", "out"):
        compare_parser.add_argument("--" + option, required=True)
    compare_parser.set_defaults(run=compare)
    args = parser.parse_args()
    if getattr(args, "velocity", 1) > 127:
        parser.error("velocity must be <=127")
    try:
        args.run(args)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"benchmark failed: {error}\n")


if __name__ == "__main__":
    main()
