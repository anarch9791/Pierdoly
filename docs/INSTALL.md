# Install & Build (Stages 7–9)

## Stage 7 — Source files produced

| Filename | Role |
|---|---|
| `firmware/ceiling_motion.h` | Pure motion/layout core (no WLED deps). Unit-tested. |
| `firmware/usermod_ceiling_mover.h` | The WLED usermod + custom effect. |
| `firmware/usermods_list_ceiling.cpp` | Reference for the 2-line registration edit. |
| `platformio_override.ini` | Build env for GL-C-618WL-4 Advanced. |
| `test/test_motion.cpp`, `test/Makefile` | Host test/visualiser (numbered menus). |
| `docs/*.md` | Design, install, Home Assistant. |

---

## Stage 8 — Adding the files to the WLED source tree

Assume the WLED 0.16.x source is checked out at `~/WLED`.

1. **Create the usermod folder and copy the two headers:**

   ```
   mkdir -p ~/WLED/usermods/ceiling_mover
   cp firmware/ceiling_motion.h        ~/WLED/usermods/ceiling_mover/
   cp firmware/usermod_ceiling_mover.h ~/WLED/usermods/ceiling_mover/
   ```

2. **Register the usermod** — edit `~/WLED/wled00/usermods_list.cpp` and add the
   two guarded blocks shown in `firmware/usermods_list_ceiling.cpp`:

   - with the other `#include`s near the top:
     ```cpp
     #ifdef USERMOD_CEILING_MOVER
     #include "../usermods/ceiling_mover/usermod_ceiling_mover.h"
     #endif
     ```
   - inside `registerUsermods()`:
     ```cpp
     #ifdef USERMOD_CEILING_MOVER
     usermods.add(new CeilingMover());
     #endif
     ```

   This is the **only** edit to WLED core source.

3. **Copy the build config** to the WLED root:

   ```
   cp platformio_override.ini ~/WLED/platformio_override.ini
   ```

4. **Verify the porting tags.** Search the usermod for `VERIFY:` and confirm
   each API name matches your exact WLED revision (they are stable in 0.16.x but
   check): `strip.addEffect`, `Segment::setUp`, `Segment::setMode`,
   `getPixelColor`, `strip.fill`, `Bus::setGlobalAWMode/getGlobalAWMode`,
   `setTransition`, `setMainSegmentId`, and the effect data-string format. If
   any differ, adjust the single call — no structural change is needed.

---

## Stage 9 — Build configuration (GL-C-618WL-4 Advanced)

The controller is a standard ESP32 (WROOM); no board variant is required. Build
the env defined in `platformio_override.ini`:

```
cd ~/WLED
pio run -e ceiling_glc618wl4
# flash:
pio run -e ceiling_glc618wl4 -t upload --upload-port /dev/ttyUSB0
```

Key build flags (already in the file): `-D USERMOD_CEILING_MOVER`,
`-D CEILING_STRIP_LEN=864`, `-D WLED_MAX_BUSSES=4`, `-D WLED_MAX_SEGMENTS=8`,
`-D ABL_MILLIAMPS_DEFAULT=0` (external 5 V PSU; enable the limiter later if you
want it).

### LED-settings (in the WLED web UI after first boot)

Configure four outputs, SK6812 RGBW, colour order **GRBW**, contiguous:

| Output | GPIO (verify on unit) | Type | Start | Length | Reverse |
|---|---|---|---|---|---|
| 1 | 16 | SK6812 RGBW | 0   | 216 | as needed |
| 2 | 3  | SK6812 RGBW | 216 | 216 | as needed |
| 3 | 1  | SK6812 RGBW | 432 | 216 | as needed |
| 4 | 4  | SK6812 RGBW | 648 | 216 | as needed |

- Total 864 LEDs; four buses of 216 with no gap/overlap ⇒ one continuous logical
  strip and no boundary artefacts (Stage 4).
- **Reverse**: with the controller at the midpoint, tick Reverse on whichever
  runs are wired so their physical order opposes the logical index direction, so
  that a block sweeping 0→863 moves smoothly end-to-end.
- Set the **RGBW / auto-white mode** here (Accurate/Brighter/Max); it can also be
  switched at runtime from Home Assistant (see `HOME_ASSISTANT.md`).

### First check

Set a solid colour and brightness in the UI. Then from Home Assistant (or a
`curl` POST to `/json/state`) trigger a layout move and confirm the block slides
with no flash and no colour change. Example:

```
curl -X POST http://<wled-ip>/json/state \
  -H 'Content-Type: application/json' \
  -d '{"CeilingMover":{"layout":2}}'
```

> Note: `layout` in the JSON API is **0-based** (0 = Layout 1 … 4 = Layout 5).
> The Home Assistant helpers in `HOME_ASSISTANT.md` present 1-based choices and
> subtract 1 for you.

---

## What has and hasn't been compiled

- `ceiling_motion.h` is **compiled and fully tested** on a PC — build and run
  `test/` (`make test`), 260 assertions cover every 1↔2 block transition plus
  ON/OFF for all edges.
- `usermod_ceiling_mover.h` is written against WLED 0.16.x APIs but **must be
  compiled inside the WLED tree** (it needs `wled.h`); it was not built in
  isolation. Work through the `VERIFY:` tags during your first `pio run`.
