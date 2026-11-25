# Compatibility

The current native hooks require the Windows Steam Rocksmith 2014 Remastered x86 executable with SHA-256:

`bb056959c0c6371d4ecf78f84c5d27e2fae93a9d0258830c2a36f33e6a2778eb`

Check a local file using `Get-FileHash -Algorithm SHA256 -LiteralPath "<file>"`. Do not upload the executable to an issue. Unknown game images or unexpected instruction signatures leave the classifier/intro hooks uninstalled. A selected controller filter can still operate independently.

RSMods is optional. The runtime does not load it, inspect its config, require its hash, call its helpers or modify its vtables. Other mods can still conflict with the same game patch sites, so coexistence needs separate testing. RSMods' faster library scanning remains its feature and is not included in this mod.

The protection helper tries Windows VirtualProtect, the exported ntdll API, and an own WoW64 adapter on x64 Windows. The latter reads and validates the current service number from the local Windows library instead of assuming one. Unrecognized stub layouts, native 32-bit Windows and ARM64 do not enable that direct path. The scheduling option uses Windows process power-throttling APIs; missing APIs leave it inactive. Other Windows builds, game revisions and loaders are not qualified merely because the DLL loads.

Keep any RS ASIO installation and its settings separate. Profile selection currently stays manual; no RSMods.ini settings are required for our mod. The generic prototype config keeps both input options off for gamepad support. Another mod owning `dinput8.dll` is a loader conflict to resolve before installation.

Startup movie recovery has a fixed-image compatibility check for the tested separately installed RSMods build (SHA-256 `ae1566434f39634cee8c5192da809c797ac976657aec9fa0ed42026ec21b1024`). This optional recognition is not a runtime requirement or a call into RSMods. Unknown pre-existing menu-update detours leave intro acceleration off. A later hook replacement remains subject to that other mod's load order and behavior.
