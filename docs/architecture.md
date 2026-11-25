# Runtime architecture

The x86 `dinput8.dll` exports the observed DirectInput interface and forwards to the absolute Windows system DLL. Optional wrappers filter ANSI/Unicode enumeration callbacks while preserving keyboard/mouse device creation and COM identity/refcounts. Other QueryInterface requests can return an unwrapped interface; this prototype is not a general-purpose DirectInput replacement.

Normal export initialization reads the config, initializes logging and validates native hosts. It installs each selected input/intro feature independently. The classifier skips a redundant WMI-based XInput query only inside a thread-local known-device callback scope; unknown calls use the original function through a verified trampoline.

The native adapters require the exact game hash and expected instructions/data. They do not load or call RSMods. The game is not modified on disk. Patched modules are pinned until process exit so hook targets cannot be unloaded. Removing or replacing files requires closing the game.

## Early scheduling

The DLL captures its attach time and optionally applies the process scheduling hint during DllMain using a bounded config read and Kernel32 APIs. It does not load modules, create a thread/timer, hash files, initialize COM or wait under the loader lock. Normal initialization pins the DLL and arms a threadpool restoration timer. Original execution-speed bits are restored and read back while preserving unrelated current hints.

Explicit unloading before timer setup restores the hint. After runtime hooks/timer setup the module is pinned. A late first export means restoration happens at that later initialization rather than at an independently armed attach-time deadline. See [configuration.md](configuration.md) for failure logging and timing details.

## Intro completion

Only the native intro controller's second `mPlaybackState` getter CALL is redirected. The custom ABI passes the movie in EAX and the engine string in ECX, returning a float in x87 ST0. The adapter calls the original getter, preserves unready/ready/playing states 0, 1 and 2 and unexpected values, and maps only stopped state 3 to finished state 4. No shorter stock intro was established in the standalone test. The game's own controller performs cleanup and transitions to title.

The hook must preserve CALL semantics. An early prototype emitted JMP at this site and crashed; the corrected implementation emits CALL. Its fixture includes an EBP frame and live stack locals so returning to the wrong place cannot accidentally pass as a legal tail call. The failed prototype was rolled back and is not the current code.

## Standalone protection changes

For the recognized game, `memory_protection.hpp` first prepares and prefers the validated own WoW64 adapter to avoid slow rejected wrapper calls. Other contexts first try the documented process-local `VirtualProtect` API. If it fails, it tries the exported `NtProtectVirtualMemory` function. On supported x64 Windows/WoW64, a further own native-call adapter uses the OS transition from the x86 TEB. It reads the current service number from the Windows ntdll file identified by the loaded module, validating PE bounds, the export, its stub pattern and five-argument cleanup. No service number or third-party function address is fixed in the source. Missing/unknown layouts or failed protection changes leave the target unpatched. No Windows implementation bytes are included in the distribution. Other architectures retain the ordinary API paths.

The classifier rolls back its original bytes and releases its trampoline if restoring page protection fails. The protection fixture exercises the real exported-native and own WoW64 paths, unaligned native regions, old-protection output, denied operations, missing API handling and RX restoration. The classifier fixture covers failed-restore rollback before a successful install.

## Profile selection deferred

The previous RSMods vtable/tick adapter has been removed from the runtime and build. The game presents its normal profile selection. A future native transition should omit the picker during startup and expose selection as a main-menu option; that behavior is not implemented here. Legacy `AutoLoadLastProfile` values have no effect and produce a diagnostic marker.
