# Ceiling Mover — WLED moving-block firmware

Custom WLED 0.16.x firmware for a ceiling-mounted **SK6812 RGBW** installation
(6 m, 864 LEDs, 144 LED/m) on a **Gledopto GL-C-618WL-4 Advanced** (ESP32, four
outputs). Most of the time the light is an ordinary static WLED light; when you
switch between predefined layouts, the illuminated block(s) **physically slide**
to the new position — accelerate from rest, cruise at a configurable speed,
decelerate smoothly, and stop exactly on the destination. Not a fade, wipe,
dissolve or crossfade.

Everything runs **locally** in the firmware. No external Python, no DDP, no Home
Assistant frame streaming, no continuous network traffic to hold the image.

## What's here

```
firmware/
  ceiling_motion.h            Pure motion + layout planning core (no WLED deps)
  usermod_ceiling_mover.h     WLED usermod: effect, state machine, HA API
  usermods_list_ceiling.cpp   The 2-line WLED registration edit (reference)
platformio_override.ini       Build env for the GL-C-618WL-4 Advanced
test/
  test_motion.cpp             Host test + ASCII visualiser (numbered menus)
  Makefile
docs/
  DESIGN.md                   Stages 1-6: architecture, model, RGBW, risks
  INSTALL.md                  Stages 7-9: files, WLED tree edits, build, LED cfg
  HOME_ASSISTANT.md           Stage 11: HA controls
```

## The five layouts

| # | Illuminated LEDs | Blocks |
|---|---|---|
| 1 | 0–863 (full) | 1 |
| 2 | 100–400 | 1 |
| 3 | 500–800 | 1 |
| 4 | 50–200 **and** 700–800 | 2 |
| 5 | 300–563 | 1 |

Moving into a 2-block layout **splits** the source; moving out of one
**converges** it. Interior two-block layouts are fully supported (no
"always at the ends" assumption).

## Try the motion logic right now (no hardware)

```
cd test
make          # builds ./test_motion
make test     # runs the automated suite (260 assertions)
./test_motion # interactive numbered menu: visualise any transition / ON / OFF
```

The test harness includes the exact same `ceiling_motion.h` that the firmware
uses, so it validates the real planning and kinematics — split, converge,
pairing, acceleration-from-rest, cruise, and landing exactly on target.

## Build & install for hardware

See `docs/INSTALL.md`. In short: drop the two headers into
`WLED/usermods/ceiling_mover/`, add two guarded lines to
`wled00/usermods_list.cpp`, copy `platformio_override.ini` to the WLED root, and
`pio run -e ceiling_glc618wl4`. Configure four SK6812-RGBW buses of 216 LEDs
(0/216/432/648) in the LED-settings UI.

## Colour safety

During a move the effect re-emits the **already-resolved per-pixel RGBW** it
sampled at move start — the same bytes, only shifted — and lets WLED's normal
bus pipeline (auto-white, brightness, colour order) run unchanged. No second
RGB→RGBW conversion, no gamma/temperature/brightness change. The white channel
and overall brightness stay exactly as in the settled state. RGBW mode
(Accurate / Brighter / Max) is switchable from Home Assistant and is global, so
static and moving output always match.

## Status

- Motion core (`ceiling_motion.h`): **compiled and tested** on PC — 260/260 pass.
- Usermod (`usermod_ceiling_mover.h`): written to WLED 0.16.x APIs; compile it
  inside the WLED tree and review the `VERIFY:` tags on first build (they flag
  the handful of helper calls whose signatures vary between WLED revisions).

See `docs/DESIGN.md` for the full rationale behind every reliability requirement
(no black frame, no flash, no reversion, no realtime timeout, no boundary
artefacts, locally settled static state).
