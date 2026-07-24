# ForceChallenge

A general **force-complete any challenge** mod for HITMAN: World of Assassination, built on ZHMModSDK.

## What it does

HITMAN unlocks are **server-granted**: the IOI server re-derives challenge completion from the uploaded gameplay **event stream**, not from any client-side "completed" flag. A memory edit that just shows `5/5` is cosmetic and gets reconciled away. This mod instead **emits the genuine qualifying gameplay events** into the live event funnel on the game thread, so the server re-derives the challenge and grants its reward (e.g. the **Druzhina 34 ICA** for *Quintuple Trouble*).

## Hard precondition (online)

Events only reach the server when the online-send gate (`dword_1430EA4B8`) `== 2` — i.e. **active online play / a live session**. Offline, injected events serialize locally and are never sent ⇒ no unlock. The mod **refuses to inject** when the gate isn't open and **never writes** that gate (forcing it points the sync at a dead connection → error/crash).

## Two ways to build a type-faithful event

* **Construct** — parse the captured JSON payload with the game's own JSON deserializer. Engine-owned, numeric types match real events. Works without needing a live sample (use this for *Quintuple Trouble*).
* **Capture-replay** — arm capture, trigger the target event **once** in-game; the mod deep-copies that real event and replays it `N` times. The most type-faithful (it *is* a real event), pure-SDK.

## Build

1. Add the mod to the build list in the repo-root `CMakeLists.txt` — append `ForceChallenge` to `set(MODS …)`:
   ```cmake
   set(MODS
           …
           Assets
           ForceChallenge
   )
   ```
2. Configure + build ZHMModSDK as usual (`cmake --preset …` / your existing flow). `ForceChallenge.dll` lands in the mods output dir.
3. In-game, open the ZHMModSDK menu (default `~`/`Insert`) → **FORCE CHALLENGE**, or `load ForceChallenge` from the SDK console.

## Use (Quintuple Trouble, default preset)

1. Get into the mission, **online**, actively playing (the window shows *Online send gate: OPEN*).
2. Mode = **Construct**; the Event JSON is pre-filled with the `MultiKillTargets` payload and the Drops GUID / Challenge Id are pre-filled for reference.
3. Click **Build & Verify (no inject)** → status should read *event JSON OK (valid dict)*.
4. Set **Repeat count** = `goal − current` (3/5 ⇒ `2`).
5. Click **FORCE COMPLETE**. Emission rides real gameplay events — stay in active play until **emitted** reaches your count.
6. Verify in-game that the challenge shows complete and the reward is granted; exit to **planning** to refresh the loadout so the unlock arms.

For other challenges: edit the Event JSON (`{Name, ContractId, Value}` only — the funnel adds `UserId/SessionId/Origin/Id/Timestamp`). `$inc` counters → repeat the same event; `$pushunique`/distinct-set challenges → use capture-replay, or emit distinct payloads.

## Build-specific addresses

Resolved off the runtime module base from these (analysed build, imagebase `0x140000000`):

| Symbol | Address | Role |
|---|---|---|
| funnel | `0x140CB0860` | `funnel(ZEventsManager*, ZDynamicObject*)` — enqueues to SM + outbound |
| events mgr | `0x1430EAEA0` | `ZEventsManager` singleton (funnel `this`) |
| parser | `0x14151DCB0` | generic JSON deserializer `(result, data, len, flag, ZString* label)` |
| online gate | `0x1430EA4B8` | `== 2` ⇒ events are sent |
| dict sentinel | `0x1445B3DC0` | `STypeID*` for a dict (event-validity check) |

If the game updates, **re-resolve these by AOB** and update `ForceChallenge.cpp` (the `kOff*` constants). The `0x14151DCB0` parser is the *generic* deserializer — do **not** use the SDK's `Functions::ZJsonDeserializer_Deserialize`, whose AOB binds to the loadout parser (`0x14094B810`) and will crash (it expects a `ZString*` where a raw `char*` is passed).

## Status / honesty

This source is **design-complete and crash-safety reviewed** (game-thread-only injection, full re-entrancy guard, injected-event memory retained against use-after-free, safe empty-ZString parser label, online-gate gating). It has **not** been compiled or run in-game by the author. Before trusting it:

1. Build it and confirm it loads.
2. In a **supervised** online session, do the **safe one-event check first**: set Repeat = 1, emit once, and confirm the challenge counter advances / the outbound sync fires / the server reacts **before** emitting the rest. If nothing reacts, stop — the server is rejecting/deferring and a different payload or timing is needed.

A CheatEngine equivalent that the same procedure can drive live (no build step) is `force_challenge.lua` — see the companion write-up `HITMAN-Quintuple-ForceComplete.md`.
