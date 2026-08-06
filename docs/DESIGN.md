# Ceiling Mover — Design

This document covers specification stages 1–6: WLED architecture, the division
of responsibility, the data model, the four-output mapping, RGBW preservation,
and where black-frame / colour-mismatch risks live and how the design removes
them.

---

## Stage 1 — Relevant WLED 0.16.x architecture

The pieces this project touches:

- **Effects (`FX.cpp` / `FX.h`)** — each animation is a `uint16_t mode_xxx()`
  function that writes pixels into the *current segment* (`SEGMENT`) and returns
  the delay in ms until it should next run. Since WLED 0.14 an effect can be
  **registered at runtime** with `strip.addEffect(id, fn, dataString)`, so a
  usermod can add a new effect **without editing `FX.cpp`/`FX.h`**. We use this.
- **Segments (`Segment` / `WS2812FX`)** — a segment is a pixel range with a
  mode, colours and brightness. The "settled" static light is just one or two
  `Solid` (`FX_MODE_STATIC`) segments — ordinary WLED state, fully controllable
  from the UI and Home Assistant, essentially free to render.
- **Bus manager (`BusManager` / `Bus`)** — owns the physical outputs. It applies
  colour order, **global brightness**, and the **RGB→RGBW auto-white**
  conversion, then pushes to the LEDs. Effects never see this stage; they write
  logical RGBW and the bus layer converts identically for static and moving
  states. This is the key to colour safety.
- **Usermods (`Usermod` base, `usermods_list.cpp`)** — a class with `setup()`,
  `loop()`, JSON-state and config hooks. This is where all state and the
  Home-Assistant API live.
- **JSON state/config (`/json/state`, `/json/cfg`)** — how Home Assistant and
  the UI talk to a usermod (`addToJsonState` / `readFromJsonState` /
  `addToConfig` / `readFromConfig`).

No realtime / DDP / UDP-notifier path is used anywhere, which is what causes the
classic "1-second reversion", "realtime timeout" and "needs an external script"
problems. They cannot occur here because that subsystem is never engaged.

---

## Stage 2 — Division of responsibility

| Concern | Lives in | Why |
|---|---|---|
| Layout geometry, block split/merge/pair, kinematics | `firmware/ceiling_motion.h` (pure, no WLED) | Unit-testable on a PC; the *same* file compiles into the firmware. |
| Custom effect registration + per-frame pixel writes | `firmware/usermod_ceiling_mover.h` (`mode_ceilingMove` + `renderFrame`) | Runtime `addEffect` ⇒ no core-effect edits. |
| State machine, colour capture, settle, JSON/HA API, RGBW mode | the usermod | Single owner of state; survives normal WLED operation. |
| Settled static image | WLED `Solid` segments | Native, static, UI/HA controllable, no continuous heavy render, no network. |
| Physical output mapping | WLED LED-settings (4 buses) | Zero code; bus manager guarantees a continuous logical strip. |

Only **two guarded lines** are added to one existing WLED file
(`usermods_list.cpp`) to register the usermod. Nothing else in core changes.

---

## Stage 3 — Internal data model

Defined in `ceiling_motion.h`:

- **`Block { int start, end }`** — inclusive pixel span. `width()`, `center()`.
- **`Layout { int count; Block blocks[2] }`** — `count` 0 (off) / 1 / 2.
- **`MovingBlock { float srcStart,srcEnd, dstStart,dstEnd; int colorIndex }`** —
  float spans so a block can translate *and* resize smoothly (split/merge), and
  may sit off-screen (negative or ≥ stripLen) for enter/exit. `colorIndex`
  selects which captured source colour paints it.
- **`TransitionPlan { int count; MovingBlock blocks[2] }`** — the whole move.
- **`MotionParams { maxSpeed, accelFrac, cruiseFrac, decelFrac, minDuration,
  maxDuration, fps }`** — all configurable, none hard-coded.

Colour and brightness are deliberately **not** in this layer. The usermod holds
`uint32_t _layoutColor[]` (settle colour per layout) and captures live
`_blockColor[]` at move start. This guarantees the pure layer can never alter a
colour.

### Kinematics

A cosine-eased trapezoidal velocity profile: smooth **accelerate from rest →
constant-speed cruise → smooth decelerate to rest**, landing *exactly* on the
destination. Because the eased ramps have average height ½, the area under the
normalised velocity curve is `A = 0.5·accelFrac + cruiseFrac + 0.5·decelFrac`.
The duration is solved so the longest-travelling block cruises at exactly
`maxSpeed`:

```
T = maxDistance / (maxSpeed · A)     (clamped to [minDuration, maxDuration])
```

All blocks share one `T` and one eased progress `s(t) ∈ [0,1]`, so they start
and stop together. The previously preferred feel (accel 0.8 / decel 0.8 /
cruise 0.2) maps to `accelFrac 0.4, cruiseFrac 0.2, decelFrac 0.4` — an 0.8/0.2
accel-vs-cruise split, symmetric decel. Change any value at runtime via JSON or
in config.

### Transition planning (all cases proven by the test suite)

- **1→1** — one block translates/resizes to the destination.
- **1→2 (split)** — the source block divides into two sub-ranges sized in
  proportion to the destination block widths (not necessarily equal), each
  travelling to its destination block.
- **2→1 (converge)** — the destination is partitioned into two sub-ranges
  proportional to the source block widths; the two source blocks move in and
  meet to form one contiguous region.
- **2→2** — blocks are paired by sorted position (left→left, right→right), the
  minimal-crossing / most-logical assignment.
- **ON** — blocks slide in from off-screen (left / right / both / nearest).
- **OFF** — blocks slide off-screen toward an edge; settled state = off.

Two-block layouts are **not** assumed to be at the strip ends — planning works
for any positions (e.g. Layout 4 is `50–200` & `700–800`, both interior).

---

## Stage 4 — Four outputs → one continuous logical strip

The GL-C-618WL-4 has four data outputs. 864 / 4 = **216 LEDs per output**.
Configure four buses in WLED LED-settings, contiguous and in order:

| Bus | Start index | Count | Physical run |
|---|---|---|---|
| 1 | 0   | 216 | first half of strip A |
| 2 | 216 | 216 | second half of strip A |
| 3 | 432 | 216 | first half of strip B |
| 4 | 648 | 216 | second half of strip B |

Because the buses are contiguous with no gaps or overlaps, the WLED bus manager
presents them as a single logical strip `0…863`. The effect only ever addresses
logical indices, so:

- **No visible discontinuity** between outputs — they are one address space.
- **No duplicated/skipped pixels** — each logical index maps to exactly one
  physical LED, guaranteed by the start/count values above.

Controller-at-midpoint wiring: pick a consistent data direction so logical 0 is
at one far end and 863 at the other. If a physical run is wired "in?wards", set
that bus's **Reverse** checkbox in LED-settings — this only reverses that bus's
output order and keeps the logical range continuous. See `INSTALL.md`.

---

## Stage 5 — Preserving exact RGBW during movement

The rule: **never reconstruct a colour; move the value that is already there.**

1. At the instant a move begins, for each source block we read the
   already-resolved per-pixel value with `strip.getPixelColor(center)` — a
   32-bit `RGBW` that WLED's static renderer produced (post-effect, pre-bus).
2. Every animation frame re-emits *that exact `uint32_t`* into the shifted
   pixels via `SEGMENT.setPixelColor(p, capturedColor)`. No RGB→RGBW re-mapping,
   no colour-temperature change, no gamma/brightness rescale in the effect.
3. Downstream, the **same** bus pipeline (auto-white mode, global + segment
   brightness, colour order, gamma) is applied to the moving pixels exactly as
   it was to the static ones. Identical input value + identical pipeline ⇒
   identical apparent colour, white contribution and brightness.

This is precisely what fixes the previous "white channel / overall brightness
became stronger while moving" bug: that came from converting RGB→RGBW a second
time in the animation path. Here there is no second conversion — the resolved
RGBW is copied verbatim.

The RGBW auto-white *mode* (Accurate / Brighter / Max) is global
(`Bus::setGlobalAWMode`) and shared by both states, so switching it from Home
Assistant changes static and moving output together — never a mismatch.

---

## Stage 6 — Where a black frame / mismatch could appear, and the design around it

| Risk point | Cause | Design |
|---|---|---|
| Swapping settled→animation | switching effect can insert a crossfade or blank | We collapse to one full-strip segment and set our effect with `setTransition(0)`. The effect's **first frame (s=0) reproduces the current image exactly**, so the swap is invisible. |
| End of animation | switching back can flash / go black / crossfade | The **last frame (s=1) is pixel-identical to the settled layout**. `finalizeToSettled()` lays down `Solid` segments with `transition=0` on the next `loop()`, replacing an already-identical image. No black frame, no flash. |
| "1-second reversion" | realtime/DDP timeout restoring the old effect | Never used. Purely local. |
| Colour shift while moving | second RGB→RGBW conversion | Eliminated — see Stage 5. |
| Output boundary artefacts | mis-sized buses | Fixed by the contiguous 4×216 map (Stage 4). |
| Holding the final image | needing a heavy effect or network to persist | `Solid` segments are the normal WLED static state — trivial to render, persist locally, fully controllable. |

The net effect: transitions look like a physical block of light sliding to its
new place, with no flash, no colour change, and a final state that behaves like
any ordinary static WLED light.
