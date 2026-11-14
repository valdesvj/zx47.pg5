# AI Assistance Policy

## How this project uses AI

This project was **developed with the help of AI** — Claude (Anthropic), Gemini (Google), and Grok (xAI) — as a cooperation. AI assisted with brainstorming, code generation, and documentation, but I reviewed all output, validated results against reference implementations, and take full responsibility for the work and any mistakes.

## Responsible use

- ✅ **I checked the results** — the Z80 core passes zexdoc; the machine is tested on the PC (boot, keys, tape, snapshots); speed and keys are checked on my DM42n
- ✅ **I maintained domain expertise** — I guide the AI with the design decisions: the DM42n's screen, keys and memory, and what is playable on a calculator
- ✅ **I take full responsibility** — The work is mine; any errors are mine to fix
- ✅ **I'm transparent** — This file documents how AI was used

## Testing & Validation

- Every screen checked in the PC simulator (`make shots`), and `make test`, before a commit
- The LCD layout is learnt on the calculator at start (no undocumented detail assumed)
- Timings measured on real DM42n hardware (FPS47 in GB47)

## License

This does not affect the GNU GPL-3.0 license. All code remains open-source and fully accessible.

---

*For more details, see `NOTICE` and the README.*
