# Authored chord voicing: exact 1.005 runtime boundary

Promoted from the authorized read-only `ChordVoicingRuntimeFeasibility-1.005.md`
in `ff7rp-mod963-20260905-audit`, including its final lifecycle disposition.
Source review base: `1630adc59c4066b79845b05a84aa9c86020d2818`.
Ghidra program `ff7rebirth_.exe-9457d4`, project `/ff7rebirth_.exe-1.005`,
image base `140000000`, Windows x64. PE timestamp `6a16ced2`, image size
`099d9000`, checksum `0769ea6e`; retained executable SHA256
`752807180c4ed919667ff0ec163046410187e873888dd75248e78aaa72e363dc`.
The loaded Ghidra image's full hash was not freshly obtained. This is static
evidence, not executable invocation, implementation validation or gameplay proof.
No 1.004 instruction-path authority is established by asset parity.

## Two entry hooks, one owner

`piano_score_expand` and `piano_chart_update` remain exclusively `note_count`.
New chord callback/cached-row copier entry hooks belong only to `chord_voicing`.
The research independently uniqueness-scanned these exact entry/neighborhood bytes:

| RVA | Role | Bytes |
|---|---|---|
|039b263c|charted chord callback|48 89 5c 24 10 48 89 4c 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8b ec b8 80 00 00 00 e8 22 95 7b fe 48 2b e0 41 80 78 4a 02 4d 8b f8|
|039893c4|cached-row copier|48 89 5c 24 08 57 b8 30 00 00 00 e8 ac 27 7e fe 48 2b e0 48 8b 02 48 8b da 48 89 01 48 8b f9 48 8b 42 08 48 89 41 08 48 8b 42 10 48 83 c2 18|
|039b2740|copier caller neighborhood|c6 83 88 01 00 00 01 48 8b d6 e8 75 6c fd ff 49 8b 47 2c|
|039b278d|span loads|48 63 46 30 4c 8b 76 28 c5 78 28 c0 48 8d 0c 40 49 8d 04 8e 4c 3b f0 0f 84 bc 00 00 00|

Terminal caller window RVA `039a682d` bytes
`48 8b 0f 48 8b 58 20 e8 bb 92 ff ff 4c 8b c7 48 8b ce c5 f8 28 c8 ff d3 48 8d 4c 24 20`
was read directly, NOT independently uniqueness-scanned. CALL RBX at `039a6843`
returns `039a6845`. Chord-mode vtable RVA `05bc28d0`, slot +20 targets callback.

Callback ABI: byte AL result, RCX owner, XMM1 float tempo, R8 event.
Copier ABI: RCX original dst, RDX original cached value, RAX dst result.
Call at `039b274a` returns `039b274f`. Original copier MUST execute exactly once
with unchanged arguments: persistent owner+f8 receives stock data. The loop
ignores RAX and uses RSI; immediately after return RAX is overwritten from
event+2c. RSI is used only for +30 count/+28 data loads at the inspected span,
then ESI becomes a local capacity. Native outer epilogue restores saved RSI.
Thus only this exact caller permits a deliberate RSI return handoff AFTER all
C++ calls. RDI/saved original entry, config/socket/text and native cache remain stock.
The adapter needs normal stack alignment, unwind metadata and a register harness.

## Native values and borrowed lifetime

Cached value: input packed FName +0, config ID +8, socket +10, text +18,
array header +28/+30/+34. Records are packed 0x0c `{uint64 FName,float velocity}`.
Native reader bound five; stock verified widths are three/four. Override width
must fit original slots; slot velocity is inherited BEFORE filtering, never
filled with 75 or renumbered after filtering. Runtime cache width remains a
checked native invariant (other asset mods are not implicitly supported).
IgnoreSound helper `14076ba10` compares full packed FName qwords, not pitch:
Ab2 != Gs2. Nonmembers suppress nothing. Native loop retains exact filtering,
RNG, tempo/duration, deep copies and native immediate/delayed voice ownership.
Zero survivors and an empty stock-filtered batch need no special emitter.

Event stride90: chart+0, side+8, parent+10, head/next+18, ordinal+20,
ID+2c, IgnoreSound+38, selector+49. Canonical plan orders monotone then chord;
source ordinal is row*2/row*2+1. Group followers are eligible, not new inputs.
Chart header+80/+88/+8c is events E, not source rows R or parentless actions A.
Chart+118 captures controller; controller+f48 chart/+f50 nonnull control block;
left/right sides controller+380/+538. Side selected index+1a0 indexes
16-byte shared-pointer entries in array+0/count+8. Terminal caller locks the
selected owner strongly for the synchronous callback; no mod refcount writes.
Per-call range/stride/ordinal/selector/ID/backlinks/controller/selected-owner
checks are borrowed native validity, not UObject serial or asynchronous ownership.

## Admission, readiness and retirement

One immutable preflight descriptor/plan/mapping binding belongs to existing
selection guard/playback/cleanup. Allocate/resolve/capability-check BEFORE chart
journal writes or audio arm. Guard publication transfers the pending binding;
post-prefix/tail completion only validates and seals the stable native header.
Current playback alone grants projection; compare binding and CURRENT same-lease
token under registry mutex, native calls outside. Legitimate route-token updates
remain supported. Frames pin immutable storage until synchronous return.
Same-wrapper reparse invalidates before original reconstruction. Cleanup does not
authorize sound; failed chart identity remains retained until real retirement.

Existing `piano_chart_update` at `1439ba628` owns pre-original readiness rejection.
Recovered state entry `143999af4` precedes state9 update `1439b5290` synchronously
through `1421bb2c8`/`1421b26c8`. Native wrapper constructor `143247914` sets vtable
`146569688`; slot20->`143258b18` updates its controller at wrapper+30.
External serialization/reentrancy was NOT proved. The existing chart tick gates
all recovered chart judgement/result/follower/input paths before original;
parent UI/freeplay is outside its scope. Pending input is explicitly REJECTED,
not buffered or replayed; no timer/wait/poll count grants readiness.
Unexpected native invariant failure withdraws playback/ScoreInfo through the
existing audio owner and retains cleanup/list-pending ownership. It is not native
Stop/free, fake list return, score rollback, or universal OOM/SEH recovery.
