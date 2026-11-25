# Runtime architecture

The x86 `dinput8.dll` exports the observed DirectInput interface and forwards to the absolute Windows system DLL. Optional wrappers filter ANSI/Unicode enumeration callbacks while preserving keyboard/mouse device creation and COM identity/refcounts. Other QueryInterface requests can return an unwrapped interface; this prototype is not a general-purpose DirectInput replacement.

Normal export initialization reads the config, initializes logging and validates native hosts. It installs each selected input/profile/intro feature independently. The classifier skips a redundant WMI-based XInput query only inside a thread-local known-device callback scope; unknown calls use the original function through a verified trampoline.

The native adapters require exact game/RSMods hashes, additional image metadata and expected instructions/data. The game is not modified on disk. Patched modules are pinned until process exit so hook targets cannot be unloaded. Removing or replacing files requires closing the game.

## Early scheduling

The DLL captures its attach time and optionally applies the process scheduling hint during DllMain using a bounded config read and Kernel32 APIs. It does not load modules, create a thread/timer, hash files, initialize COM or wait under the loader lock. Normal initialization pins the DLL and arms a threadpool restoration timer. Original execution-speed bits are restored and read back while preserving unrelated current hints.

Explicit unloading before timer setup restores the hint. After runtime hooks/timer setup the module is pinned. A late first export means restoration happens at that later initialization rather than at an independently armed attach-time deadline. See [configuration.md](configuration.md) for failure logging and timing details.

## Intro completion

Only the native intro controller's second `mPlaybackState` getter CALL is redirected. The custom ABI passes the movie in EAX and the engine string in ECX, returning a float in x87 ST0. The adapter calls the original getter, preserves missing/unready state 0 and unexpected values, and maps initialized states 1–3 to finished state 4. The game's own controller performs cleanup and transitions to title.

The hook must preserve CALL semantics. An early prototype emitted JMP at this site and crashed; the corrected implementation emits CALL. Its fixture includes an EBP frame and live stack locals so returning to the wrong place cannot accidentally pass as a legal tail call. The failed prototype was rolled back and is not the current code.

## Boot profile selection

The adapter wraps the pinned RSMods profile mod's verified vtable entries. It calls the original tick only at the initial picker after structural checks on mirrored profile-row vectors and engine string lengths. It waits for readiness, bounds retries, expires after the initial picker window, and stops after selection or later state changes. It does not cancel network dialogs, sign in, identify a personal profile name or change profile files.

The remembered first row is an observed game behavior, not an explicit stable profile-ID contract. Actual picker bypass remains separate work.
