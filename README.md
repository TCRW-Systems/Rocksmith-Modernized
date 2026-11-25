# Rocksmith Modernized

Experimental startup improvements for the Windows Steam edition of Rocksmith 2014 Remastered. The installed mod consists of **`dinput8.dll` and `RSStartup.ini`**. RSMods is optional; RS ASIO remains separate if you use it. The standalone runtime uses Windows APIs directly.

The project is in early development. The standalone code passes offline checks; real-game validation is documented in [validation status](docs/validation.md). Earlier dependency-based builds passed two consecutive boots on one setup. It has not passed the release stability gates or the ten-second startup target. The earlier RSMods-dependent build observed approximately 29.4 seconds from Steam Play to the title state; the current standalone test observed approximately 54.3 seconds. Its conservative intro path needs further optimization. Library-loading frame-rate problems remain open.

## Features

| Option | Behavior |
| --- | --- |
| `SkipIntro` | Experimental stopped-state intro completion without archive edits. Faster timing on the stock UI is not yet established. |
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

## Configure and install

Read the compatibility requirements first. Close Rocksmith. Copy `startup/RSStartup.example.ini` to a working file and enable the options you want; the example defaults every feature off. `startup/RSStartup.prototype.ini` instead enables intro skipping and scheduling while preserving gamepads.

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

## Package

```powershell
python tools/package_startup.py --dll build/startup/dinput8.dll --config startup/RSStartup.prototype.ini --output build/packages/RocksmithModernized-prototype.zip
```

The ZIP contains exactly the selected DLL and config. The script verifies their contents and refuses an existing output. It excludes game assets, upstream mods, reports, backups and test executables.

## Development and reporting

Read [architecture](docs/architecture.md), [validation status](docs/validation.md), and [CONTRIBUTING.md](CONTRIBUTING.md) before proposing changes. Diagnostic captures and crash dumps can contain private information; review and redact them before sharing. The defaults in `.gitignore` keep local evidence and build output out of the repository.

Credits and external dependencies are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This is an independent community project, unaffiliated with Ubisoft.

## License

Licensed under the [MIT License](LICENSE). External game and mod content remains outside this license; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
