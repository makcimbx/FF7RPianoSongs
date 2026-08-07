# Piano Page Negative-Selection Guard Evidence (2026-08-03)

## Supported executable and historical failure

This evidence applies only to catalog build `ff7rebirth-steam-win64-6a16ced2`
(SHA-256 `752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`).
The historical session
`20260803T103429Z-622320eda78c` retained log SHA-256
`07d0fb223e6bf1dbd50fb3af0cacd49298e096cc77bddcd4ffe4031f5d5580eb`.
It reproduced a null-`RCX` fault at visibility-setter RVA `0x00889f79` while
playing Find the Flame, profile 3, difficulty 4, with 238 notes.

The temporary paired page-rebuild/visibility diagnostic source is retained in
Debug stash `5162d32c51b6e747b6910e391165907d1ed33922` on base
`259184db4c8fe0748470f8b25c36a63d01471d5f`. It is evidence only and is not
part of the durable implementation.

## Decisive differential evidence

Runtime-gate session `20260803T115917Z-d49a748a7c37` used artifact SHA-256
`67c87dccce7e6ef371a27b86c4ca09a1a2ff80ab395cfc10adb95718dfb08b57`
and retained log SHA-256
`98b2380ffa2df96453e9d5d9d0450b4549e15b8e1e51fc0b8dca403a7e259258`.
Immediately before the identical fault RVA, the complete page array reported
requested/count 5 and capacity 6, selected index `-1`, and a null resolver
result for Find the Flame profile 3/difficulty 4/238 notes. This excludes an
incomplete page-array rebuild as the observed first divergence.

Research task `ses_0417d2417ffebvKxytj5QX94sa` established that `-1` is the
native no-selection sentinel and that native old-index handling treats every
negative value as no index. The exact correction is therefore limited to the
selected weak-resolver CALL at RVA `0x039b04f9`.

## Exact durable contract

- The original call bytes are `E8 FE 44 E8 FC` and decode to weak resolver RVA
  `0x008349fc`.
- The following bytes are `BA 01 00 00 00 48 8B C8`; the next CALL decodes to
  visibility setter RVA `0x00889f6c`. The no-selection continuation at RVA
  `0x039b050b` re-reads the model selection, stores the signed result at
  `owner+0x88`, sets the dirty bit, and continues native processing. Startup
  validates this complete resolver/visibility/continuation sequence.
- For `R12D < 0`, the relay does not dereference page storage or call either the
  resolver or selected visibility setter. It discards the patched CALL return
  and branches to the continuation. Values below `-1` use the same bypass and
  may emit `unexpected_negative_bypassed`.
- For `R12D >= 0`, the relay tail-jumps the original resolver. Its return and
  the original visibility flow are unchanged, including unresolved handles.
- The relay preserves Win64 stack alignment and nonvolatile registers. Its
  marker helper is `noexcept`; native execution never unwinds through generated
  relay code.

The direct rel32 mutation is required at startup, installed while callback
admission is closed, and owns only RVA `0x039b04f9`. Exact bytes, decoded
targets, page protection restoration, and final live state are verified. A
torn or protection-drifted call is unsafe and fails release-hook installation.
No live teardown is attempted; a reachable relay is retained whenever the live
instruction might still reference it.

Because an unsafe live instruction cannot be made harmless by merely leaving
the product callback gate closed, an `UnsafeMutation` installation result is a
process fail-fast condition. This prevents later execution of a malformed or
protection-drifted call site. Ordinary signature, allocation, or zero-mutation
failures remain clean startup failures.

The permanent marker is capped at eight identities and deduplicated by
playback generation plus selected index. Identity is copied through try-only
playback and registry snapshots and includes visible index, base slot, and a
storage-qualified catalog revision when exact. No page pointer, UObject,
descriptor owner, or snapshot survives logging.

## Exclusions

The durable candidate contains no page-rebuild hook, page-array classifier,
TLS rebuild correlation, visibility-call patch, null-setter suppression,
upper-bound guard, index normalization, or list/profile/chart mutation. It does
not retain page owners or UObjects. In-game qualification remains separate from
the decisive diagnostic evidence.
