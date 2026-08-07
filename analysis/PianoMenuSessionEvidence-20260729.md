# Piano Menu Session Evidence (2026-07-29)

This record preserves bounded matching-build evidence for the Phase 4
profile/list coordinator. Durable ownership rules belong in
`../docs/Architecture.md`; current disposition belongs in
`../docs/CurrentStatus.md`.

## Build Identity

Static analysis used Ghidra program `/ff7rebirth_.exe-9457d4`, image base
`0x140000000`. Its PE timestamp `0x6a16ced2`, image size `0x099d9000`, and
checksum `0x0769ea6e` match catalog build
`ff7rebirth-steam-win64-6a16ced2`. These are the fields enforced by the runtime
version gate. No binary or Ghidra metadata was mutated.

## Controller, List, And State Machine

- The piano controller is a native subobject. It is not a UObject and must
  never substitute for UObject identity.
- The list member is embedded at controller `+0x6f0`.
- Its exact list-widget weak handle is at list-member `+0x7a0` (controller
  `+0xe90`). Existing pointer/internal-index/serial validation remains required.
- Its active/closing word is at list-member `+0x7d8` (controller `+0xec8`):
  `0x0000` is inactive, `0x0001` is active/open, and `0x0100` is closing.
- The generic state-machine transition processor calls the current state's exit
  exactly once, installs the pending state, then calls the new state's enter.
  Controller destruction bypasses this processing.

State-5 ABI recovered from the table and dispatcher:

```cpp
using StateEnterFn = void(__fastcall*)(void* controller);
using StateUpdateFn = void(__fastcall*)(void* controller, float delta);
using StateExitFn = void(__fastcall*)(void* controller);
```

The one-argument exit ABI was rechecked after a misleading four-parameter
decompiler rendering. The controller tick calls the generic dispatcher with the
state machine, controller, and frame delta. Its transition processor loads the
current exit pointer and executes `mov rcx,rdi; call rdx`; it supplies no other
callback arguments. At the state-5 exit target, `RCX` is saved as the controller
and is the only incoming value read semantically. Incoming `RDX` still contains
the called function pointer and is overwritten before use; `R8` is dispatcher
residue and is overwritten; `R9` is uninitialized volatile residue. The apparent
fourth parameter exists only because unchanged `R9` reaches a later call whose
callee does not read it. Incoming XMM registers and callback stack arguments are
also unused. Inventing extra C++ parameters would therefore contradict the
recovered dispatcher contract; the detour must forward exactly the controller.

## Authoritative List Writers

Targeted field-write searches, list-vtable review, direct caller analysis, and
decompilation of the piano-controller region found three writers in this object
graph:

| Event | RVA | ABI and evidence |
|---|---:|---|
| Initialize/reset | `0x039a3bbc` | Writes `0x0000`; construction/setup reset only. |
| Open/reopen | `0x039b8bf4` | `void __fastcall(list_member*, uint64_t selected_entry)`; the sole inactive/closing-to-active writer. |
| Close | `0x039a1af4` | `void __fastcall(list_member*)`; the sole active-to-closing writer. |

Open prologue/signature:

```text
48 89 5c 24 08 48 89 74 24 10 57 b8 40 00 00 00 e8 77 2f 7b fe
```

This complete-instruction prefix has one match in the mapped image. The first
19 bytes have two matches; 20 bytes distinguish this function. The rel32 call is
image-relative and not a PE base relocation, so the repository's exact all-`x`
matching policy is appropriate.

The open helper has one direct caller in state-5 update. The call return is RVA
`0x0399ef23`. It validates the list widget and inactive state, writes `0x0001`,
starts native open UI, stores the selected entry, publishes list count and rows,
then restores/clamps selection. The active write precedes reflected row and
selection callbacks. It accepts `0x0000` for initial open and `0x0100` for
reopen, clearing closing state by writing the whole word.

Close prologue/signature:

```text
40 53 b8 30 00 00 00 e8 80 a0 7c fe
```

This complete-instruction prefix has one match in the mapped image. Ten bytes
are the shortest unique prefix; the full call is retained for maintainability.

The shared list-close call return is RVA `0x0399f485`. Matching-build Ghidra
analysis shows the activation action query and cancel branches converge on this
same call at RVA `0x0399f480`. The activation branch obtains the selected entry
at return RVA `0x0399f420`, requests state 6, and jumps to the shared close.
Accepted close writes `0x0100` before native close work. Cancel can leave state
5 active and later reopen a distinct list session. State-5 exit also calls the
close helper from a different call site. Volatile `BL` is not semantic authority;
reservation confirmation and exact identity proofs distinguish preservation
from cancel/teardown.

## Terminal Session Seams

| Event | RVA | Evidence |
|---|---:|---|
| State-5 exit | `0x039a6254` | `void __fastcall(controller*)`; exactly once for every processed transition from state 5. |
| Controller destructor | `0x039886cc` | `void __fastcall(controller*)`; destroys list/state objects without invoking state exit. |

State-5 exit signature:

```text
48 89 5c 24 08 57 b8 60 00 00 00 e8 1c 59 7c fe
```

This complete-instruction prefix has one mapped-image match. Thirteen bytes
have nine matches; fourteen bytes distinguish this function.

Controller-destructor signature:

```text
48 89 5c 24 20 55 56 57 41 56 41 57 48 8d 6c 24 c9 b8 a0 00 00 00 e8 99 34 7e fe
```

This complete-instruction prefix has one mapped-image match. Twenty-three bytes
have four matches; twenty-four bytes distinguish this function.

Activation requests state 6; the next controller tick invokes state-5 exit.
Unsupported and failure paths request other states and receive the same exit.
The destructor is the required fallback for teardown that bypasses transition
processing.

## Required Authority Model

One `menu_session` hook owner must install open, close, state-5 exit, and
controller-destructor detours as a mandatory transaction. All catalog entries,
prologues, and the two exact call sites must validate before attachment; partial
attachment must roll back all four hooks.

The open call at RVA `0x0399ef1e` is exactly `e8 d1 9c 01 00`, uniquely matches
the image, decodes to the canonical open helper, and returns at `0x0399ef23`.
The shared activation/cancel close call at RVA `0x0399f480` uses signature
`e8 6f 26 00 00 84`: the call plus first following opcode uniquely matches,
decodes to the canonical close helper, and returns at `0x0399f485`. Keep the
return RVAs for detour filtering and represent the call instructions as
signature-gated `call_site` catalog entries. All selected signatures use exact
`x` masks; no wildcard or runtime relocation adjustment is required.

Open allocates a nonzero monotonic generation while the native word is inactive,
publishes `Opening`, calls native exactly once without holding the menu mutex,
then requires the same owner/generation, `0x0001`, and exact list-widget UObject
identity before publishing `Ready`. Reflected callbacks during native open are
scoped to that `Opening` generation.

Native open publishes rows and restores selection synchronously before it
returns, so the final reflected list callback can occur while the session is
still `Opening`. After native return and successful exact `publish_ready`, the
detour must notify the profile/list coordinator once with the retained opening
generation, before returning. This notification is not a synthetic list callback:
it may drain only an already-bound intent whose session, setup identity,
selection, UI identity, and readiness predicates still match exactly. A stale or
repeated notification is a no-op.

Filtered shared list close marks the matching generation `Closing` and calls
native once. If the word became `0x0100`, an exact `Reserved` activation with a
confirmed handoff and exact terminal menu, reservation, list-widget UObject,
registry selection, unconditional live BGM controller identity, and conditional
substrate identity transitions `Pending` to `Preserved` before the menu authority
is retired. Unconfirmed, unowned, or nonexact pending ownership is reclaimed as
cancel/teardown. A rejected native close restores `Ready` without changing audio
ownership. State-5 exit is preservation fallback only when shared close did not
already retire the session; duplicate close/exit cannot revoke `Preserved`,
`ClaimedFrozen`, `Consumed`, `AdmissionOwned`, or newer ownership. Destructor and
reopen retain exact reclaim semantics.
Pointer reuse cannot revive authority because controller/list pointers, exact
list-widget UObject identity, and generation must all match. Generation wrap
disables session-dependent behavior fail-closed.

The menu mutex must never be held across native, registry, selection, or audio
operations. Terminal identity is copied under that mutex, then conditional audio
revocation runs under the existing reservation-to-registry lock order. Raw
controller identity never substitutes for BGM-controller or UObject proof.

## Bounded Disposition

Static evidence is sufficient to implement and offline-review a reversible
candidate. It does not qualify runtime behavior. One user-controlled scenario
must still cover immediate profile input during list return, cancel/reopen with a
new generation, activation/return, coherent title/rank/selection/chart/audio, and
absence of stale-session effects or continuing old audio.
