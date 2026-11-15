# ZX47: working rules

- Work on a new branch; Victor creates and merges the PRs. Never force-push or rewrite history.
- Commit author: "Victor Valdes" <valdesvj@users.noreply.github.com>.
- The same base as Chess 47, NAV47, TERM47 and GB47 (dmcp/, tools/pg5tool.py, test/sim): keep them in step.
- third_party/z80 is superzazu/z80 (MIT) with ZX47's changes marked at its top: after a change, make test-z80.
- DMCP5: 8 KB of static RAM. Big buffers go on the heap (the 64 KB memory, the screen copy); check .bss.
- make test and make shots before committing; look at every screen. The simulator's timings mean nothing.
- No commercial games in the repo: only the Amstrad ROM (with permission), our own demo and the Z80 exercisers.
- Ask before anything that can't be undone. Pull requests only for zx47.pg5.
