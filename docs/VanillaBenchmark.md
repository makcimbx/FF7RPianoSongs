# Offline vanilla piano benchmark

`tools/vanilla_piano_benchmark.py` exports decoded, provenance-checked stock tables
to isolated MIDI song folders and compares generated `.cache/resolved-song.json`
against the original chart. It uses Python's standard library, no game process,
production cache, parent checkout, or transcription dependency.

## Inputs and execution

Supply JSON lists in the existing decoded-table format:
`[{"table":"PianoScore","rows":[{"RowName":"Journey", ...}]}]`.
The corpus needs `PianoScore` and `PianoScoreInfo`. Assignment JSON needs
`PianoMonotoneAssign`, `PianoChordsAssign`, `PianoChordsConfig`, and `PianoSound`.
Both options can name one JSON containing all six tables. Their original property
names are retained; absent array tails default to empty names or zero, and
`IgnoreSound_Array` has **three flattened slots per chart row**.

Use an explicitly verified base-container extraction, not assets whose directory
merely says “Vanilla.” Extraction and executable/container provenance are external
to this portable tool. For example, the optional parent-repository adapter is
`Tools/decode_ff7r_piano_dataobject.py --out corpus.json <six extracted assets>`.
Keep game binaries, extracted assets, and generated corpus outside Git. Do not
replace retained mod evidence or force-add ignored baseline files.

```powershell
python -B tools/vanilla_piano_benchmark.py export `
  --corpus C:/evidence/corpus.json --assignments C:/evidence/corpus.json `
  --out C:/evidence/fresh-benchmark --gate-frames 12 --series-gap-frames 60

# Run the freshly built offline tool from BuildAndRelease.md, once per song folder:
& C:/build/song_cache_tool.exe C:/evidence/fresh-benchmark/Journey

python -B tools/vanilla_piano_benchmark.py compare `
  --baseline C:/evidence/fresh-benchmark/Journey/baseline.json `
  --assignments C:/evidence/corpus.json `
  --resolved C:/evidence/fresh-benchmark/Journey/.cache/resolved-song.json `
  --out C:/evidence/journey-comparison.json

python -B -m unittest discover -s tools -p test_vanilla_piano_benchmark.py
```

Export refuses a nonempty destination. Each song folder contains `song.mid`,
generator-only `song.json`, neutral synthetic `song.wav`, `baseline.json`, and
`original-chart.song.json`. The last file is an authored-chart control, not a
second automatically selected input. To compile that control, copy it as
`song.json` alongside the WAV into a separate fresh folder. This separates source
decoding/export fidelity from generator selection losses.

## What is exact, assumed, and measured

- Onsets are original `seconds_frame` at 60 fps. SMF uses 60 ticks/quarter and
  exactly 60 BPM, so one tick equals one native frame. **60 BPM is a timing
  carrier, not a claim about musical tempo**; stock `Bpm` may be zero.
- Chords expand through verified assignment → configuration → sound names.
  Followers retain their own sounding onsets but are not additional required
  inputs. Silent filtered chords still retain their original required action.
- Known `IgnoreSound` names are filtered literally, not by pitch class. Stock
  slots without a chord and unmatched known names remain explicit in the sidecar;
  unmatched names are reported on events. They do not silence a differently named
  or different-octave constituent. Unknown assignments/sounds fail, not disappear.
- Original note/dot notation, strength, hand IDs, raw control fields, groups,
  roots, and source info remain in the sidecar. Note symbols are **not recovered
  sustain**. MIDI note-offs use the explicitly chosen gate and truncate on a
  same-channel/pitch retrigger. Coincident duplicate sounds merge only in MIDI.
  Velocity is an explicitly reported constant (default 75), not an exact
  performance reconstruction. No pedal or sustain inference is attempted.
- The WAV is an unrelated, low-level, constant **synthetic 1 kHz analysis signal**.
  It permits an isolated full pipeline run; it is not stock music, a listening
  reconstruction, or evidence for audio-prominence choices. No real stock-audio
  roundtrip or prominence calibration is implied.
- Workload reports required-input counts, hand shares, all per-hand gaps,
  uninterrupted series, and raw assignment-field transitions between required
  actions. The explicitly supplied series-gap cutoff is a reporting choice,
  **not a universal human timing limit**. No joystick angle, neutral-return,
  ergonomic difficulty, quotas, or target density is inferred.
- `joint` reports counts and exact frames with required inputs from both hands,
  and frames where both hands change native control fields from their respective
  previous required inputs. First inputs have no preceding change; followers do
  not affect either measure. Two simultaneous roots are two inputs but one joint
  time. These are descriptive coincidences, not physical movement-cost estimates.
- Music recall/precision match distinct **onset + sounding MIDI pitch**, regardless
  of hand/root/follower. Required-root retention separately requires the original
  **onset + hand + assignment ID**. Matching uses compiled 60-fps timing exactly,
  with no hidden tolerance. Enharmonic sounding pitches match musically; distinct
  original input assignments do not become the same root. Thus an alternate
  assignment can reduce root-identity retention without losing a musical attack.
- Each retention measure includes a deterministic, sorted `missing` list to locate
  omitted passages: musical records contain `frame` and MIDI `pitch`; root records
  contain `frame`, `hand`, `assignment_id`, and missing multiplicity `count`.
  Musical list length and summed root counts equal their respective aggregate
  losses. No salience or motif importance is inferred from these lists.
- Root `precision` and `additional` records also expose newly required actions,
  including sounds that were originally automatic followers. An additional root
  need not be an invented sound; alternate assignment IDs also count separately.

Compare every emitted profile independently. A missing generated difficulty is
not silently substituted, and this tool imposes no profile quotas or pass grade.
Custom chord voicings are outside this stock-only benchmark and rejected.
