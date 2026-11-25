# Configuration and operation

`RSStartup.ini` lives beside the game executable. All values are boolean `0` or `1` under `[Startup]`:

```ini
[Startup]
SkipGamepadEnumeration=0
SkipKnownKeyboardMouseXInputChecks=0
SkipIntro=1
StartupHighQoS=1
```

This is the generic prototype preset. `startup/RSStartup.example.ini` instead defaults every feature off. Restart the game after changing options.

Enable both input options only for keyboard/mouse use. The first hides other devices from the wrapped DirectInput enumeration callbacks, and the second shortcuts classification only for the exact known keyboard/mouse product GUID. It does not uninstall devices or change their system settings. Leave both off for gamepad support.

The profile auto-load adapter has been removed. Profile selection follows the game's normal flow. Legacy `AutoLoadLastProfile` values are ignored, with a diagnostic marker if enabled. The future native startup bypass/main-menu profile option is a separate implementation pass.

`SkipIntro` accelerates the intro's own timeline and enables bounded recovery for startup UI requests lost during file indexing. Original playback states and callbacks are preserved; the intro may still flash briefly. With the exact tested RSMods recovery hook already installed, request recovery is left to that mod. Unknown conflicting hooks leave intro acceleration off. The feature does not modify `cache.psarc`, `static.psarc` or the game executable on disk.

`StartupHighQoS` requests an explicit process execution-speed hint; it does not change CPU affinity, priority, a power plan or the registry. The restoration deadline is about 50 seconds after DLL attach. The timer is armed at normal export initialization outside DllMain. If initialization occurs after the deadline, restoration is attempted immediately then. Restoration checks the state, preserves unrelated hint bits and uses bounded timer retries. Failures are recorded in `RSStartup.log`.

## Installation records

Use `tools/startup_install.py` from the repository root with explicit `--game`, `--dll`, `--config` and a new `--record` directory. Keep records outside the game and private. A record includes local paths, backups and hashes. Do not delete it until rollback is no longer needed.

Fresh installations refuse existing target files. `--replace-existing` requests an intentional update, not automatic compatibility with another proxy. Multiple updates create separate layers; roll back the newest first. `resume --record` continues an interrupted installation; repeating `rollback --record` resumes an interrupted rollback. The tool refuses unrelated target edits and changed backups. It does not overwrite profiles or songs.

The source also retains `tools/apply_intro.py` and `tools/apply_profile_boot.py` as legacy experiment/recovery utilities and test dependencies. They are not part of the current installation path. No staged archive assets or historical installation records are distributed. Use the current `startup_install.py` for new installations.

## Diagnostics

```powershell
python rocksmith_diag.py --game "<Rocksmith directory>" snapshot
python rocksmith_diag.py --game "<Rocksmith directory>" record
```

These read-only diagnostics do not install the mod or launch the game. Recording waits for an executable matching the selected game directory. Output goes under private `reports/` by default. It can include installation paths, configuration/log copies and existing dumps, which may contain process memory. Review captures before sharing. An archive count is not a song count, and a faulting module alone does not identify a crash cause.
