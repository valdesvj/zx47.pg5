# AI Assistance Policy

## How this project uses AI

This project was **developed with the help of AI** — Claude (Anthropic) — as a cooperation. AI assisted with brainstorming, code generation, and documentation, but I reviewed all output, validated results against reference implementations, and take full responsibility for the work and any mistakes.

## Responsible use

- ✅ **I checked the results** — The Z80 core passes the standard instruction exercisers (prelim, zexdoc: `make test-z80`), and the 48K machine is tested against the real Spectrum ROM: it boots, takes keys, loads tapes and snapshots (`make test`)
- ✅ **I maintained domain expertise** — I guide the AI with the design decisions: the DM42n's memory and speed (measured with FPS47), the keys, the screen and how the games should feel on the calculator
- ✅ **I take full responsibility** — The work is mine; any errors are mine to fix
- ✅ **I'm transparent** — This file documents how AI was used

## Testing & Validation

- The Z80 exercisers prelim and zexdoc (zexall with `make test-z80 ZEX=all`)
- The machine: boot to © 1982 Sinclair Research Ltd, BASIC typed, the demo tape, `.SNA` and `.Z80` round trips (`test/zxtest.c`)
- Every screen checked in the PC simulator (`make shots`) before a commit
- Tested on real DM42n hardware, on battery and on USB power (Knight Lore, with its music)

## License

This does not affect the GPL-3.0-or-later license. All code remains open-source and fully accessible.

---

*For more details, see `LICENSE` and the README.*
