# Validation status

The corrected combined DLL passed two consecutive unchanged launches on one development setup with RSMods, RS ASIO and a large library of 2,339 DLC archives. An archive count is not a song count.

| Check | Observation |
| --- | --- |
| Process creation to observed title state | 29.194 seconds, then 26.358 seconds |
| Steam Play action to observed title state | 29.359 seconds on the second run |
| Title response to Enter | Passed in both runs |
| Boot-only profile auto-load | Passed in both runs |
| Library access | Passed in the first run |
| Manual profile re-entry | Passed in the first run |
| Scheduling restoration readback | Logged at 50.113 and 50.100 seconds |
| Corrected second run exit | Normal close, code 0 |
| Protected executable, mod, archive, audio/display hashes | Matched the saved installation baseline |
| Offline suite in the built development workspace | 19 Python tests and 8 native fixture modes passed |

These are development observations with a warm machine, not a controlled performance median, cross-machine result, audio/input endorsement or crash-rate claim. Raw captures, dumps, paths, profile names and backup records remain private and are not included in this repository. Earlier prototypes include failures; the CALL/JMP bug described in [architecture.md](architecture.md) was corrected before these two launches.

The prepared public folder also passed a fresh build, all 19 Python tests and all eight native fixture modes. CI deliberately excludes real-game automation and the three desktop-dependent fixture modes. The GitHub-hosted workflow has not yet been run.

## Open work

- The target of about ten seconds from Steam Play to a responsive title is unmet.
- Frame-rate problems during the full library scan remain unresolved.
- Profile selection still appears briefly. Explicit profile identity and actual picker bypass remain future work.
- Ubisoft connection/profile-loading time has not been eliminated.
- Other game/RSMods binaries, loaders, Windows builds and machines need separate validation.
- The generic prototype config preserves gamepads; this is different from the keyboard/mouse-only configuration used in the two combined-build game runs.

## Proposed release gates

Before calling a build stable, complete 20 clean launches, 30 song cycles, 100 practice seeks across at least five songs, three one-hour gameplay sessions, preserved profiles/progress and song availability, and verified removal/rollback. Exercise Ubisoft connectivity, Steam lifecycle, keyboard/mouse, any supported gamepad and actual audio/guitar input. Startup, library scan and gameplay are separate milestones.

For performance claims, record Steam Play, process creation and the first visible responsive title separately; distinguish warm runs from a first run after Windows restart. Measure the full library without deleting songs to improve results. Report failed attempts and regressions alongside successful timings. Do not use a faster internal marker as a substitute for an interactive screen.
