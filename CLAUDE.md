# Pierdoly — Ceiling Mover WLED firmware

Custom WLED 0.16.x usermod for a ceiling SK6812 RGBW strip on a Gledopto
GL-C-618WL-4 (ESP32). See `README.md` and `docs/`.

- `firmware/ceiling_motion.h` — pure motion/layout core, no WLED deps
- `firmware/usermod_ceiling_mover.h` — WLED usermod (effect, state machine, HA API)
- Host tests: `cd test && make test`

## Subagents

Project subagents live in `.claude/agents/` and are committed to git so they
are available on every device and in cloud sessions after `git pull`. The full
guide (format, fields, checklist) is in `.claude/SUBAGENTS.md`.

When asked to create a subagent:
1. Follow `.claude/SUBAGENTS.md`.
2. Write it to `.claude/agents/<name>.md` in this repo — never to
   `~/.claude/agents/`, which exists on one machine only.
3. Commit and push it so other devices receive it.
