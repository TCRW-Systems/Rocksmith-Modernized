# Validation status

## Current startup recovery build

The RSMods runtime dependency and boot-profile adapter remain removed. The current source builds and passes 19 Python tests and nine native fixture modes. Added native coverage checks the intro property-accessor ABI, original playback preservation, simultaneous title/overlay requests, request arguments, delay/frame gates, completion/ID cancellation, bounded capture and RX restoration.

The final DLL passed a launch with both RSMods files absent, followed by two launches with them restored. Loaded-module inspection confirmed each configuration. All three showed a visible Begin prompt, responded to Enter, connected normally to Ubisoft and reached manual profile selection. Scheduling restoration readback succeeded around 50 seconds after attach. No songs were removed; the installation contains 2,339 DLC archives, which is not a song count.

| Final-build run | Configuration | Steam Play to title-state marker | Visible Begin observation |
| --- | --- | --- | --- |
| 1 | RSMods absent | 23.926 s | Present at the later 33.821 s screenshot |
| 2 | RSMods restored | 24.907 s | Absent at 28.428 s; present by 33.463 s |
| 3 | RSMods restored | 25.054 s | Absent at 27.225 s; visible by 29.748 s |

The last run's screen observations were scheduled near 24, 27 and 29.5 seconds. The 29.748-second value is an upper bound from the returned screenshot timestamp; it is not an exact first-frame measurement. Enter was checked afterward. These are warm development launches, not a reboot/cold-cache benchmark, median or guarantee that every launch completes in under 30 seconds. Native title-state markers can precede a usable screen and must not substitute for visible verification.

The startup regression came from leaving the full stock intro in place to avoid a lost-request soft lock. The current implementation accelerates the original animation, preserving its callbacks, and supplies bounded startup movie-request recovery. With RSMods absent, the log verified retries and successful creation of both affected startup movies. If the tested external recovery hook already owns the update site, ours defers; a later external replacement can also own menu updates. Startup smoke tests do not establish every mod/load-order combination.

Failed experiments are retained in private evidence: early ready-state completion and timeline acceleration without recovery left the loading overlay stuck. A title-only retry created the title movie but left the overlay missing; the final capture queue includes both. The prior conservative build passed title/Enter checks but observed about 54-57 seconds from Play to its title marker. These failures are not successful timing samples.

Profile selection is deliberately manual until the later native startup bypass/main-menu option. RSMods' faster song scanning is not supplied here. No long-session, crash-rate, complete library-scan, gameplay or audio/guitar claim is made for this revision.

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

The prepared public folder also passed a fresh build, all 19 Python tests and all nine native fixture modes. CI deliberately excludes real-game automation and the three desktop-dependent fixture modes. The initial GitHub-hosted run on commit `4e59798` compiled successfully but failed the launcher integration test: Windows expanded the runner's short TEMP path while the test compared literal path spellings. The test now compares directory identity with `samefile`; a real 8.3-alias reproducer failed before this change and passed afterward. All 19 Python tests and six synthetic native checks pass locally after the fix. Checkout and Python setup actions were updated to their Node 24 versions. A hosted rerun of these local fixes is still pending a push.

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
