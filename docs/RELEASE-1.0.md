# ZX47 1.0 — a 48K ZX Spectrum for the SwissMicros DM42n

First release. Spectrum games and BASIC on the DM42n: Knight Lore runs with its music.

**Files**
- `ZX47.pg5` — the program (DMCP5 3.57 or later). Copy it to the calculator's disk, then **Load program → ZX47.pg5**.
- `ZX47DEMO.TAP` — a small BASIC demo: put it in a `ZX47` folder on the disk, then **EXIT → F3**.

**What it does**
- 48K Spectrum with the original ROM (© Amstrad, with permission), at the real speed on USB power
- loads `.Z80`, `.SNA` and `.TAP` (tapes load at once); saves the game as a `.SNA`; off keeps the game
- the picture 1:1, only the changed lines sent to the LCD; colour or ink mode, invert
- keys as TERM47 (letters on the calculator's legend keys); ALPHA / NUM / JOY layers; Kempston, cursor and Sinclair joysticks
- the beeper on the buzzer, screenshots (SHIFT +), DMCP's setup menu (USB disk, System), About

**Not yet:** 128K games and the AY chip, `.TZX` tapes, memory contention.

See the [manual](docs/MANUAL.md). GPL-3.0-or-later © 2026 Victor Valdes; written with the help of AI (`AI_ASSISTANCE.md`).
No commercial games included. Please don't contact SwissMicros about this program.
