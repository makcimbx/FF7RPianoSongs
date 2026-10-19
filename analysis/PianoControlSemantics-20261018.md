# Piano control semantics: retained-static assurance boundary

## Scope and disposition

2026-10-18, bounded **Debug research** within the wider Integration task. Source
review base: `bd12681ffaaa586cfe3ff3b286374e838d8e5096`. This investigation changes
no runtime source, hook, catalog, or game state. It does not qualify gameplay.

Read the current architecture/status/validation references and catalog before
querying Ghidra. `ghidra-mcp.list_instances` returned
`{"instances": [], "note": "No running Ghidra instances found."}`. No project was
started, no executable invoked, and no substitute deep disassembly was attempted.
The findings below reuse retained static evidence; **no new input-matcher
instructions or physical sector rules were recovered in this pass**.

## Identity and address authority

- Primary build: catalog `ff7rebirth-steam-win64-6a16ced2`, Steam 1.005,
  image base `0x140000000`, PE timestamp `0x6a16ced2`, image size `0x099d9000`,
  checksum `0x0769ea6e`, executable SHA-256
  `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`.
- Retained Ghidra export identity: `ChartEventAbiGhidra.txt:1-13`; retained chord
  investigation program `ff7rebirth_.exe-9457d4`, project
  `/ff7rebirth_.exe-1.005`, in `ChordVoicingRuntimeEvidence.md:3-12`. That record
  explicitly did not freshly hash the loaded Ghidra program. A matching program
  basename alone is not identity. This pass did not reauthenticate a Ghidra image.
- Exact per-build RVAs/signatures and installation ownership remain in
  `../src/game/rva_catalog.json`, not in this document. Relevant IDs:
  `piano_chart_update`, `piano_event_construct`, `piano_event_link`, `note_count`,
  `piano_event_result_callback`, `piano_adaptive_judgment`, and
  `player_input_input_key`.
- The 1.005 `piano_adaptive_judgment` and `player_input_input_key` catalog evidence
  is a **migration record**, not independent proof of input semantics. The former
  is research-only with no hook owner; the latter is the title subsystem's
  optional diagnostic hook. Neither grants a gameplay input hook or ABI.
- Separate 1.004 adaptive-handler recovery describes deduplication/result-state
  handling in that build's catalog entry. It is not evidence of physical input
  conversion, timing boundaries, or 1.005 instruction equality. Asset parity is
  not code-path parity. No new address or callable contract is proposed here.

## Narrowest retained owning paths

These are separate recovered segments, **not an end-to-end reconstructed input
matcher**. Existing function labels below are retained locators, not new RVAs.

1. **Chart scheduling:** `ChordVoicingRuntimeEvidence.md:79-86` records the
   state-entry -> state9 update -> existing `piano_chart_update` boundary and says
   its pre-original gate covers recovered chart judgement/result/follower/input
   paths. Parent UI/free-play is explicitly outside that scope. The controller's
   wrapper/virtual dispatch provenance is recorded there. This locates the
   gameplay neighborhood; it does not identify stick sampling/conversion.
2. **Terminal/charted sound dispatch:** `ChordVoicingRuntimeEvidence.md:27-65`
   retains a terminal caller invoking selected-side virtual slot `+0x20`, the
   charted chord callback, and its cached-row copy/sound span. The native caller
   strongly locks the selected side owner for the **synchronous callback**.
   Event/chart/side backlinks and vector bounds are borrowed validity, not
   asynchronous ownership or UObject serial identity. These are downstream sound
   facts, not proof of the direction accepted by the matcher.
3. **Result -> adaptive audio tier:** `AdaptiveMabfModeEvidence.md:7-10` records
   the shipping judgment callback reaching the handler cataloged as
   `piano_adaptive_judgment`. Results 1/2/4 reset the success streak and demote one
   tier; 5/6 increment success and can promote at configured thresholds. Thus the
   retained handler consumes classified outcomes. Neither the name nor these
   thresholds establishes early/late acceptance windows. Adaptive-mode threshold
   counts are not timing tolerances.

The missing segment is the mapping from sampled stick/button state and event
time/assignment to a newly classified chart result. Generic `InputKey`
diagnostics and title/menu D-pad debouncing are not substitutes for that segment.
`../docs/InGameValidation.md:93` concerns custom difficulty selection, not piano
note retrigger rules.

## What is established, plausible, and unknown

| Topic | Established evidence | Remaining boundary |
| --- | --- | --- |
| Required inputs vs followers | Native parser links adjacent nonzero group runs; only parentless events contribute to required-action count. First-row dual events remain two roots. See `PlayableGeneralizedChartEvidence.md:304-367`. | Exact matcher suppression branch, mixed-hand acceptance, child-audio/result-counter details are explicitly not fully recovered. Do not count every sounding follower as another physical input. |
| Repeating one direction | Repeated parentless events are distinct authored actions. | Unknown whether holding retriggers, neutral return is necessary, leaving/re-entering a sector suffices, or any repeat/cooldown exists. No minimum reset time is established. |
| Sector transitions | Assignment identity includes native fields, not only acoustic pitch. | Orientation, angular boundaries, sector widths/order, diagonal treatment, deadzone and hysteresis are unknown. Consecutive numeric `KeyAssign` values are not proven angular neighbors. |
| RH alternates/register | `PianoMonotoneAssignEvidence-20260905.md:69-87` proves C2-C6/C#2-C#6 alternate rows preserve `SoundName` while changing `KeyAssign` 0 -> 7 and register/socket metadata. Ordinary C7 already uses 7. | Distinct asset assignments are proven; which physical movement/modifier accepts them, including octave handling, is not. Do not infer an octave-button press from `OctaveInt`. |
| LH chord identity | `PianoDbChordEvidence-20260904.md:35-42` gives exact `pca_Db`: `KeyAssign=1`, `Semitone=2`, `Minor=0`, `PageIndex=2`, with separate constituent sounds. | Whether page/minor/semitone differences require held modifiers, edge presses, stick directions, or another state is unknown. No additive button-cost model is established. |
| Notation and judgment | Constructor stores per-hand NoteType/DotType; native note-value helpers are retained in `PianoNoteValueEvidence-20260904.md:5-17`. | Notation duration is not a recovered hit window, hold requirement, or scoring-duration rule. Fixed/asymmetric windows and speed dependence are unknown. |
| Preview/tempo | UI difficulty and speed presentation are separate (`PianoUiPresentationEvidence.md:23-55`). | No recovered prompt lead-time formula, preview horizon, speed-to-window relationship, or physical preparation budget. Audio guide impacts are not prompt preview measurements. |

It is plausible that physical direction changes, repeated-direction resets,
register/modifier changes and concurrent hands contribute different motor costs.
It is **not** established how to weight them, or even which transitions require
an additional physical edge. Do not promote that plausible model into native
control rules.

## Independent workload-analyzer implications

1. Keep musical events, parentless actions, followers and simultaneous roots
   separate. Use source-order/native grouping, not sound-note count as input load.
2. Preserve exact input identities and assignment fields. Report observed
   `KeyAssign`/`Semitone`/register and LH page/minor changes separately, without
   translating enum distance into degrees or musical semitone distance into
   physical travel. Preserve alternates even when they sound identical.
3. Same-assignment root repetitions, differing-assignment transitions, per-hand
   inter-action intervals and concurrent-root times are useful descriptive
   features. Call them assignment/temporal proxies, not measured motor difficulty.
   A key-change count alone cannot prove jack-reset effort or infeasibility.
4. Any proposed ring geometry, reset cost, modifier cost, fatigue weight or
   judgement window must be labeled a model assumption, outside recovered facts.
   Unknown control semantics must not silently become zero cost or hard pruning.
5. Compare stock routes at consistent speed separately from acoustic-note
   retention. Do not label chart gaps as musical rests or claim that passing an
   offline benchmark proves physical playability.

## Next focused evidence, not an authorized runtime experiment

When a correctly identified static Ghidra program is already available, start at
the retained chart-update neighborhood/terminal result-state writers, not the
downstream adaptive-mode handler. Trace one producer of event result `+0x4A`
backward to its time comparison and accepted input identity; then trace the
input-state producer to previous/current sector and modifier state. Retain exact
branches/constants, caller and owner provenance, signatures and the mapping
between sampled input and accepted events. An angle helper alone would not prove
retrigger behavior. Stop after one hand's same-direction/changed-direction path.

If static proof remains incomplete, the smallest future human scenario is two
ungrouped, same-assignment RH roots at comfortable spacing: compare continuous
hold, neutral-return/re-entry and leave-sector/re-entry. Then compare a neighboring
assignment using continuous roll versus neutral return. Record accepted/missed
root outcomes and actual controller motion separately from sounds. That isolates
the repeat/reset question; it does not measure numeric angular boundaries or
judgment windows. Only after that result, independently test one register or LH
modifier transition if needed. Capture speed/profile/controller/build identities.

Game launch, installation, tracing or attachment still require explicit approval
and the canonical validation gates. No live pointer or callback may be retained
past its proven lifetime. On compromised evidence, stop per
`../docs/InGameValidation.md:128-132`; no hot unload or forced native cleanup.

## Checks and gaps

Checked canonical references/catalog, retained parser/grouping, assignment,
charted-sound ownership, notation and adaptive-mode evidence; queried Ghidra
availability once. No game operations, source/catalog changes, package work,
subagents or commits. Ghidra absence is the bounded blocker to new static matcher
proof in this pass, not evidence that a particular control rule is false.
