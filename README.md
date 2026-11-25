# Rocksmith Modernized

Experimental startup improvements for the Windows Steam edition of Rocksmith 2014 Remastered. The installed mod consists of **`dinput8.dll` and `RSStartup.ini`**. RSMods is optional; RS ASIO remains separate if you use it. The standalone runtime uses Windows APIs directly.

The project is in early development. The standalone code passes offline checks; real-game observations and limits are documented in [validation status](docs/validation.md). A warm launch showed the visible Begin prompt within 29.75 seconds of Steam Play, and Enter continued normally. The current build has passed startup checks both without RSMods and with it restored. This does not establish a consistent sub-30-second cold start, the ten-second target, or release stability. Library-loading frame-rate problems remain open.

## Try the experimental tester build

**This version is suitable for limited volunteer testing.** It has passed startup checks on one development PC, with and without RSMods. Broader hardware compatibility, cold-start performance and long-session stability still need testing. It currently targets startup; library scan speed, gameplay crashes and the planned profile-picker bypass remain separate work.

### Requirements

- A Windows Intel/AMD 64-bit PC and a separately installed Steam copy of Rocksmith 2014 Remastered. Other architectures and Proton have not been qualified.
- The supported `Rocksmith2014.exe` listed in [compatibility](docs/compatibility.md). Check its SHA-256 before testing: unsupported game versions will leave native features inactive.
- The **tester ZIP supplied by the maintainer**, currently named `RocksmithModernized-startup-recovery-prototype.zip`. GitHub's source-code ZIP contains source files and requires building; it is not the tester package.

To check the executable, open PowerShell in the game folder and run:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath ".\Rocksmith2014.exe"
```

Compare the result with the hash in [compatibility](docs/compatibility.md).

The tester ZIP contains exactly `dinput8.dll` and `RSStartup.ini`. **Testers do not need Python, Visual Studio, an installer, RSMods or a Steam launch wrapper.** Keep RS ASIO if you already use it; this mod does not replace your audio setup.

### Install and launch

1. Close Rocksmith completely. Open its installation folder: the folder containing the real `Rocksmith2014.exe`.
2. Check for an existing `dinput8.dll` or `RSStartup.ini`. When updating an earlier Rocksmith Modernized build, back up both files outside the game folder first. If another mod owns `dinput8.dll`, resolve that loader conflict before installing; automatic chaining is not provided.
3. Extract **both files** from the tester ZIP directly into that folder, beside `Rocksmith2014.exe`. Do not place them in `dlc`, a subfolder, or a Windows system folder. Do not copy the synthetic `Rocksmith2014.exe` from developer build output.
4. Launch the game normally through Steam. No new launch option or registry change is required. Existing RSMods and RS ASIO files can stay in place.
5. Confirm that the Enter/Begin prompt appears and responds, then continue through Ubisoft connection and normal profile selection. Short flashes of the intro logos are expected. `RSStartup.log` is generated beside the game and can help diagnose a failed test.

The supplied config enables intro acceleration and the temporary startup scheduling hint. **Both input-filter options are off**, preserving gamepad discovery. Profile selection stays manual, and Ubisoft online features stay enabled. The observed 29.75-second warm launch used both keyboard/mouse-only filters enabled. The generic tester preset keeps them off for gamepad compatibility, so its timing can differ. That development result is not a promised time on every PC.

### Optional keyboard/mouse-only settings

For a tester who does not use a gamepad, close the game and change these two existing values in `RSStartup.ini` to `1`:

```ini
SkipGamepadEnumeration=1
SkipKnownKeyboardMouseXInputChecks=1
```

Leave both at `0` if you need a gamepad. Restart after any config change. These options filter the game's discovery calls; they do not disable devices in Windows. See [configuration](docs/configuration.md) for all four options.

### Remove or recover from a failed test

Close the game, then remove the `dinput8.dll` and `RSStartup.ini` supplied by this package. If you backed up an earlier Rocksmith Modernized version, restore that pair instead. Leave your other mods and game files in place. The generated `RSStartup.log` can also be removed. This installation does not patch game archives or the executable on disk.

If startup freezes or crashes, first check whether removing this package restores the previous behavior. To narrow a failure down, you can then test `SkipIntro=0` and `StartupHighQoS=0` separately, with both input options at `0`. Close the game between changes. Keep the original failure's log before launching again.

### What to test and report

- Compare your usual startup with the mod removed and installed. Time **Steam Play to the visible Enter/Begin prompt**, then confirm that Enter works. Report the first launch after a Windows restart separately from repeated warm launches; a blank window or loading logo is not a completed startup.
- Check Ubisoft connection, your normal profile, song availability, keyboard/mouse or gamepad, and audio/guitar input. Try your usual playing session, including library browsing, starting songs and practice-mode seeking. Report any freeze, crash or regression even if startup is faster.
- Include your Windows version, CPU, storage type, game executable hash, approximate library size, other mods and versions, the four config values, timings, and exact failure stage. State whether removing the mod or disabling an option changes the result. A short, reviewed excerpt from `RSStartup.log` is useful; avoid posting profiles, game files, complete diagnostic folders or unreviewed memory dumps.

Send results to the maintainer or open a repository issue. See [validation status](docs/validation.md) for the existing evidence and [CONTRIBUTING.md](CONTRIBUTING.md) for reporting details.

## Features

| Option | Behavior |
| --- | --- |
| `SkipIntro` | Accelerates the original intro timeline and recovers lost startup UI requests, without archive edits. |
| `StartupHighQoS` | Requests a temporary process scheduling hint near startup, then attempts and verifies restoration around 50 seconds. |
| `SkipGamepadEnumeration` | Limits the wrapped DirectInput enumeration to keyboard and mouse. Opt in only if you do not need a gamepad. |
| `SkipKnownKeyboardMouseXInputChecks` | Avoids redundant XInput classification for the keyboard/mouse currently being enumerated; requires the preceding option. |

Ubisoft online features remain enabled. The mod does not automate sign-in, edit profiles, disable Windows devices or require registry changes. It derives its paths from the game process and contains no user-specific profile names or installation paths. It writes `RSStartup.log` for diagnostics; that generated log is not a required installation file.

## Compatibility

This is an x86 mod. The native classifier and intro hooks currently require one exact game executable; see [compatibility](docs/compatibility.md) for its hash. No RSMods binary, configuration or internal function is required. Unrecognized native hook targets are left unpatched. Controller enumeration and the Windows scheduling option have their own gates.

The game, DLC and other mods are not included. A separately obtained game installation is required; other mods remain optional. No Steam launch wrapper or modified game archive is required for the current DLL implementation.

## Build and test

Requirements: Windows, Visual Studio C++ tools with x86 support, Windows SDK, the .NET Framework C# compiler, and Python 3.11 or later for tools/tests. Python tools use the standard library.

From the repository root in PowerShell:

```powershell
.\startup\build.ps1
python -m unittest discover -s tests -v
```

The build discovers the compiler and SDK; overrides are available. It writes to `build/startup` and does not install into or start the real game. The test executable named `Rocksmith2014.exe` in that directory is a synthetic harness, not the game. See [building and testing](docs/building.md) for native checks and CI details.

Profile selection currently uses the normal game flow. The old RSMods-backed auto-load adapter has been removed. A deeper startup picker bypass with a main-menu profile-selection option is planned as separate work. Legacy `AutoLoadLastProfile` settings are ignored and logged.

## Developer installer (optional)

Testers using the supplied ZIP can follow the copy-in instructions above. For a journaled developer installation, read the compatibility requirements first and close Rocksmith. Copy `startup/RSStartup.example.ini` to a working file and enable the options you want; the example defaults every feature off. `startup/RSStartup.prototype.ini` instead enables intro skipping and scheduling while preserving gamepads.

Use an explicit installation path and a new backup-record directory outside the game:

```powershell
python tools/startup_install.py install --game "<Rocksmith directory>" --dll build/startup/dinput8.dll --config startup/RSStartup.prototype.ini --record reports/my-install
```

The installer refuses an existing `dinput8.dll` or config unless you explicitly request `--replace-existing`. Another mod may already own that DLL name; resolve that conflict before replacing it. The installer records previous bytes and checks protected game/audio/display files. Keep its record until uninstalling.

```powershell
python tools/startup_install.py resume --record reports/my-install
python tools/startup_install.py rollback --record reports/my-install
```

An interrupted rollback can be resumed by repeating `rollback`. Close the game before either operation. Unrelated edits or changed backups are refused. Configuration edits made after installation must be reconciled before rollback. See [configuration and operation](docs/configuration.md).

## Build a tester package

```powershell
python tools/package_startup.py --dll build/startup/dinput8.dll --config startup/RSStartup.prototype.ini --output build/packages/RocksmithModernized-prototype.zip
```

The ZIP contains exactly the selected DLL and config. The script verifies their contents and refuses an existing output. It excludes game assets, upstream mods, reports, backups and test executables.

## Development and reporting

Read [architecture](docs/architecture.md), [validation status](docs/validation.md), and [CONTRIBUTING.md](CONTRIBUTING.md) before proposing changes. Diagnostic captures and crash dumps can contain private information; review and redact them before sharing. The defaults in `.gitignore` keep local evidence and build output out of the repository.

Credits and external dependencies are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This is an independent community project, unaffiliated with Ubisoft.

## License

Licensed under the [MIT License](LICENSE). External game and mod content remains outside this license; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
