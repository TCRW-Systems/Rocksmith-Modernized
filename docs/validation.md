# Validation status

## Standalone runtime

The RSMods runtime dependency and boot-profile adapter have been removed. The current source builds and passes 19 Python tests and eight native fixture modes. The protection tests exercise real Windows exported-native/own WoW64 calls and failed-restore rollback; passthrough checks canonical COM identity.

A real game launch with both the RSMods DLL and config temporarily absent reached the visible responsive title, normal Ubisoft connection, manual profile picker, main menu and Learn a Song library. Loaded-module inspection confirmed no game-local RSMods runtime. The classifier installed in about 63 ms, all four scoped keyboard/mouse queries were bypassed, and scheduling restoration verified at 50.110 seconds. The title-state observation was 50.975 seconds after process creation / 54.325 seconds after Steam Play. That observation is separate from the later screenshot/Enter check and does not establish a precise interactive latency.

The original RSMods DLL/config were restored byte-for-byte afterward. The same DLL then passed a coexistence boot with RSMods restored: visible responsive title, online connection and scheduling restoration verified at 50.098 seconds. Title-state observations were 53.166 seconds after process creation / 56.530 seconds after Steam Play. No long-session or crash-rate claim is made.

Earlier standalone prototypes are retained in private evidence: ordinary/exported-native protection calls were rejected in the first; the second installed the own native path but premature ready/playing intro completion left the stock loading overlay visible. The current conservative intro hook preserves states 0–2 and completes only stopped state 3. No forced-completion marker occurred in the successful stock-UI run, so a shorter stock intro has not been established. Earlier RSMods-assisted intro timing must not be claimed for this revision.

Profile selection is deliberately manual until the later native startup bypass/main-menu selection implementation. RSMods' faster library scanning is not supplied by this standalone mod. The stock library scan remained visibly slow during this smoke test; opening the library does not establish full scan completion, song parity or gameplay stability.

## Historical dependent build

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
