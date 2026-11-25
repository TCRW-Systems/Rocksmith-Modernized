# Building and testing

Use Windows with Visual Studio Build Tools or Visual Studio, the Desktop development with C++ workload including x86 tools, a Windows 10/11 SDK, the .NET Framework C# compiler, and Python 3.11 or later.

```powershell
.\startup\build.ps1
python -m unittest discover -s tests -v
```

The PowerShell build uses `VCToolsInstallDir` or `vswhere`, then discovers the installed Windows SDK. `-CompilerRoot`, `-SdkRoot` and `-SdkVersion` override discovery. `-FixturesOnly` rebuilds test harnesses without replacing the proxy or fallback launcher. Outputs stay in `build/startup`; nothing is installed into the real game.

Native checks, from the repository root:

```powershell
Push-Location build/startup
try {
    .\classifier_smoke.exe
    .\memory_protection_smoke.exe
    .\intro_skip_smoke.exe
    .\title_retry_smoke.exe
    .\early_qos_smoke.exe
    .\launcher_smoke.exe
    .\Rocksmith2014.exe
    .\Rocksmith2014.exe --passthrough
    .\Rocksmith2014.exe --early-qos
} finally { Pop-Location }
```

Check every executable's exit code; 0 means success. The included CI script does so explicitly. The last three modes exercise actual Windows DirectInput or process scheduling and may need a normal interactive Windows desktop. The GitHub workflow runs the first six synthetic fixture executables and the Python suite; it does not treat hosted CI as interactive game validation.

The harness named `Rocksmith2014.exe` is synthetic. Never copy it into your game installation or distribute it as the game. It creates a temporary test INI beside itself and refuses an existing INI. Run tests before creating a deployable config in that build directory.

Python checks use temporary fake installations and, on Windows, a copied Python process for recording tests. Launcher integration requires a completed build; it is skipped when its child fixture is absent. An unbuilt tree therefore does not exercise the complete Windows suite.

The fallback `RocksmithLaunch.exe` is retained for development and launcher tests. It is not required or included in the current two-file runtime package. If deliberately used, it sets an ownership environment marker to prevent the DLL from applying a competing scheduling restoration.

Package the runtime only after passing the required checks:

```powershell
python tools/package_startup.py --dll build/startup/dinput8.dll --config startup/RSStartup.prototype.ini --output build/packages/RocksmithModernized-prototype.zip
```

The public source tree excludes prebuilt binaries. The ZIP has exactly two entries and is separate from source-control contents. Installation and game-based testing are manual development steps, outside CI.
