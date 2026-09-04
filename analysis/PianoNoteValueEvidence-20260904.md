# Piano Note-Value Evidence — 2026-09-04

## Scope

This record bounds the side-specific `NoteType`/`DotType` materialization used by the native piano chart transaction. It combines the checked-in static event ABI with the canonical authoring policy in `src/pipeline/note_value.h`. It does **not** claim audible, input-window, scoring, completion, or animation behavior for unequal dual-hand values.

## Static native evidence

- In build 1.005, `piano_event_construct` is RVA `0x03987734`; the parser calls it separately for monotone and chord events. Its stack arguments include independent `uint8_t note_type` and `uint8_t dot_type`, stored at event `+0x4B` and `+0x4C`. See `Playable513ReserveEvidence.md` and `PlayableGeneralizedChartEvidence.md`.
- In build 1.004, the independently cataloged constructor is RVA `0x03C318CC`, with parser calls at RVAs `0x03C5A115` and `0x03C5A1A3`. The constructor/parser/event layout is recorded as matching the shared transaction contract in `PlayableExtended1004Evidence.md`.
- Compact physical order is monotone first and chord second. Consequently each emitted event consumes the pair for its own source kind; a dual row is not represented by one shared pair.
- The parser-facing DataObject exposes four separate arrays: monotone NoteType, monotone DotType, chord NoteType, and chord DotType. An absent side is represented by zero in both of that side's arrays.
- The recovered duration/value helpers and constructor path preserve the supplied pair as event metadata. They do not establish that every byte combination is playable, so runtime policy must reject values outside the reviewed authoring domain rather than clamp them.

## Recovered helper mapping and reviewed authoring domain

The recovered static duration helper maps NoteType `0..6` as follows; DotType `0` is undotted and DotType `1` applies the dotted duration:

| Value | Pair |
|---|---:|
| whole | `(0,0)` |
| dotted whole | `(0,1)` |
| half | `(1,0)` |
| dotted half | `(1,1)` |
| quarter | `(2,0)` |
| dotted quarter | `(2,1)` |
| eighth | `(3,0)` |
| dotted eighth | `(3,1)` |
| sixteenth | `(4,0)` |
| dotted sixteenth | `(4,1)` |
| native one-third beat | `(5,0)` |
| native dotted one-third beat | `(5,1)` |
| native one-sixth beat | `(6,0)` |
| native dotted one-sixth beat | `(6,1)` |

The authored supported scope is deliberately narrower: NoteType `0..4` and DotType `0..1` (whole through dotted sixteenth). NoteTypes `5` and `6`, DotType above `1`, negative values, and other bytes are rejected before native mutation. For an absent hand the required stored pair is also `(0,0)`; presence is distinguished by the side FName, so `(0,0)` remains valid whole-note metadata for a present hand.

## Retained vanilla observation

The completed retained-vanilla inspection covered **2,385 rows**. Present monotone and chord events used the recovered NoteType/DotType pairs. Among **338 dual rows**, **106** had equal complete pairs and **232** had unequal complete pairs. Considering the components independently, NoteType was equal/unequal in **127/211** rows and DotType was equal/unequal in **254/84** rows. These are bounded static corpus counts, not evidence of audible or gameplay behavior.

## Runtime decision and nonclaims

- Prefix publication writes all four arrays independently and writes `(0,0)` for an absent side.
- Compact prefix validation and direct tail construction select the exact pair by canonical event kind.
- Group/link topology, ordinals, IgnoreSound ownership, camera data, max/count publication, and rollback semantics are unchanged by notation-value selection.
- No audible behavior, judgement-window behavior, result behavior, or in-game acceptance is claimed until matching-build human runtime evidence exists.
