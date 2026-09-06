# Authored chord voicing: exact 1.004 static authority

Promoted from the authorized read-only `ChordVoicing-1.004-StaticVerification.md`
and `catalog-candidates.json` in `ff7rp-voicing-1004-audit`. Inspection baseline:
`00bc5458b63494d26445b0fa7be67e701b738f21`. Every research query explicitly selected
Ghidra program `ff7rebirth_.exe`, project `/ff7rebirth_.exe-1.004`, Windows x64,
image base `140000000`. Addresses were followed from this build's parser,
assignment/cache constructors and vtable, NOT translated from 1.005.

Fresh MZ/PE reads: e_lfanew `220`, timestamp `68fd6fde`, SizeOfImage `09bad000`,
checksum `07877c99`, AMD64. Retained catalog file identity: 126263624 bytes,
SHA256 `76bd4d539878a8759433f1cd7c7b47d5e3abda0ef41572a664d2d606a9f09dc8`.
The complete imported executable hash was NOT freshly obtained from Ghidra;
its shared installed pathname alone was not used as build authority.

## Exact required bytes and discovery

Each of these five exact windows had ONE image-wide match at its stated RVA:

| Existing catalog role | RVA | Exact bytes |
|---|---|---|
|piano_chord_callback|03c58e98|48 89 5c 24 10 48 89 4c 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8b ec b8 80 00 00 00 e8 76 60 45 fe 48 2b e0 41 80 78 4a 02 4d 8b f8|
|piano_chord_cached_copy|03c33608|48 89 5c 24 08 57 b8 30 00 00 00 e8 18 b9 47 fe 48 2b e0 48 8b 02 48 8b da 48 89 01 48 8b f9 48 8b 42 08 48 89 41 08 48 8b 42 10 48 83 c2 18|
|piano_chord_copy_caller|03c58f9c|c6 83 88 01 00 00 01 48 8b d6 e8 5d a6 fd ff 49 8b 47 2c|
|piano_chord_terminal_caller|03c4d451|48 8b 0f 48 8b 58 20 e8 0b 96 ff ff 4c 8b c7 48 8b ce c5 f8 28 c8 ff d3 48 8d 4c 24 20|
|piano_chord_span_loads|03c58fe9|48 63 46 30 4c 8b 76 28 c5 78 28 c0 48 8d 0c 40 49 8d 04 8e 4c 3b f0 0f 84 bc 00 00 00|

Event constructor `143c318cc` calls chord assignment lookup `143c30470`;
lookup xrefs locate cache initialization `143c36e78`, called by owner constructor
`143c30890`. Constructor assigns vtable **RVA 05d5f500**; relocated slot +20
targets callback **03c58e98**. The vtable is a data reference, not an
ASLR-independent absolute-pointer signature. Existing `chord_voicing` alone owns
the two entry hooks; caller/span/vtable roles remain validation-only.

## ABI, values and unchanged native ownership

Callback RCX=owner, R8=event, XMM1=tempo, AL=result. Copier RCX=original dst
(owner+F8), RDX=original cached value, RAX=dst; original must run once unchanged.
CALL `143c58fa6` targets `143c33608`, returns `143c58fab` (caller window+0F).
Return overwrites RAX from event+2C. All 178 callback instructions were inspected:
RSI saved at `143c58ea3`, selected at `143c58f89/8c`, forwarded at `143c58fa3`,
read ONLY at `143c58fe9/fed` for +30 count/+28 data, later ESI repurposed, RSI
restored at `143c59148`. Existing final RSI handoff needs no adapter branch.
Terminal `143c4d37c` invokes result first, then sound through owner vtable+20:
CALL RBX `143c4d467`, return `143c4d469` (window+18), same RCX/R8/XMM1 ABI.
Free-play sibling `143c5931c` calls the copier at `143c595a1`, a different caller.

Cache entries stride48, value+8: FName0/config8/socket10/text18/vector28/count30/
capacity34. Config reader `1423a6a00` appends 12-byte FName/float records, stops
at first empty name among five getters, defaults/clamps nonpositive velocity to0.
Stock widths3/4 and positional velocities are shared asset data, not guessed ABI:
`PianoDbChordEvidence-20260904.md` records 170-row Assign/Config parity; 1.004
Assign SHA256 `D731249203881AD63F231F1C2D388B7EFB67C8FBAB98A8C9CDA6660D14B77F8A`,
Config `1107E2C32B6E111832208C2E2B4EEFA1766C244F67E3DFD5A7FC913B4363F66A`.
Filter `1408bcc60` compares full packed FNames, not enharmonic pitch. Callback
retains native RNG/duration (`143c3be9c`), positional velocity BEFORE filtering,
20-byte requests, deep copies `143c31d5c` and `143c35d0c`, and native immediate/
delayed voice owner `143c50968`. No projection pointer survives native copying.

## Existing binding and readiness path

Setup `143c40db4` stores controller+F48 chart/+F50 controlblock, chart+118
controller; sides controller+380/+538. Expand CALL `143c4120e` -> `143c5fad8`,
return `143c41213`. Header80/count88/cap8C; event stride90, chart0/side8/parent10/
head-next18/ordinal20/FName2C/IgnoreSound38/assignment48/selector49/note4B/dot4C.
Link `143c35e90` prepends followers. Side selection `143c472dc` uses array0/count8/
index1A0, shared entries stride10; native strong promotion `1408d67a8` and release
`1413c8904` hold selected owner through synchronous sound. No mod refcount writes.

Existing chart-update RVA **03c60ffc** was independently re-scanned uniquely:
`48 89 5c 24 10 48 89 6c 24 18 56 57 41 56 b8 50 00 00 00 e8 1c df 44 fe`.
State update `143c5bd98` calls it at `143c5bf13`; inside it, follower route
`143c60f58` and input `143c4ca08 -> 1423a7300 -> 143c4c8ac` reach terminal
`143c4d37c`. Existing pre-original gate therefore precedes result/scoring.
No scheduler/thread exclusion, new hook or lifecycle algorithm is inferred.

This enables static compatibility only, NOT installed trampoline/unwind or
gameplay qualification. Full independent 1.004 monotone inventory/audible sound
resolution was not newly recovered; FName and stock-width checks remain fail-closed.
No new alternate IDs, sound ranges, velocity semantics, OOM recovery or unload claim.
