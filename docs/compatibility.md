# Compatibility

The current native hooks are pinned to these files:

| Component | Required identity |
| --- | --- |
| Game | Windows Steam Rocksmith 2014 Remastered, x86 `Rocksmith2014.exe` |
| Game SHA-256 | `bb056959c0c6371d4ecf78f84c5d27e2fae93a9d0258830c2a36f33e6a2778eb` |
| RSMods source revision | `bc2856bf99f0738cac8923cdf7b6e5aad2d55f4d` |
| RSMods runtime | Game-local `xinput1_3.dll`, observed build marker `1.2.8.4-bc2856bf` |
| RSMods SHA-256 | `ae1566434f39634cee8c5192da809c797ac976657aec9fa0ed42026ec21b1024` |
| RSMods PE timestamp / image size | `0x6add9a9c` / `0x1e5000` |

The displayed version alone is insufficient: release assets can contain different binaries. Check your local files using `Get-FileHash -Algorithm SHA256 -LiteralPath "<file>"`. Do not upload the executable or upstream DLL to an issue.

The input classifier, intro and profile adapters validate the native host and their expected code/data before installing. They call RSMods' existing memory-protection helper using its verified internal ABI. Unknown images leave those hooks uninstalled. A selected controller filter can still operate independently; this is not an all-features compatibility switch.

The scheduling option uses Windows process power-throttling APIs. Missing APIs leave it inactive. Validation so far covers one Windows setup; older Windows builds, other game/RSMods builds, other loaders and other machines are not qualified. Keep an existing RS ASIO installation and its settings separate.

For profile auto-load, RSMods.ini must contain `ForceProfileLoad=off` and an empty `ProfileToLoad` under `[Toggle Switches]`. The adapter refuses conflicting or missing values rather than overwriting that file. It uses the game's currently remembered first profile row; it does not persist an explicit profile ID or select a configured name.

The generic prototype config keeps both input options off. Users relying on a gamepad must leave them off. Another mod owning `dinput8.dll` is a loader conflict that must be resolved before installation.
