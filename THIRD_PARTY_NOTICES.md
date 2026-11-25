# External dependencies and acknowledgments

## Rocksmith 2014 Remastered

Rocksmith and associated names belong to their respective owners. A separately obtained game installation is required. This repository contains no game executable, DLC, audio, graphics, game archives, profiles, crash dumps or extracted code sections. Short instruction signatures and image hashes in the source identify supported hook targets. This project is not affiliated with Ubisoft.

## RSMods

[RSMods](https://github.com/Lovrom8/RSMods) is an optional, separately installed mod. Earlier prototypes called its loaded memory-protection helper and profile tick. Those runtime dependencies have been removed; the current code uses its own Windows API wrapper and leaves profile selection manual.

The project does not bundle RSMods source, binaries, symbols, GUI resources, Fast Load assets or third-party libraries. RSMods' song-scan improvements remain part of RSMods. Upstream implementation studied during the earlier work: [MemUtil.cpp at bc2856bf99f0738cac8923cdf7b6e5aad2d55f4d](https://github.com/Lovrom8/RSMods/blob/bc2856bf99f0738cac8923cdf7b6e5aad2d55f4d/DLL/MemUtil.cpp). Diagnostic offsets were checked against that project's public descriptions and the pinned local game.

Obtain optional mods from their maintainers. This repository's MIT license does not grant rights to their content or to the game.

## RS ASIO

[RS ASIO](https://github.com/mdias/rs_asio) is an optional, separately installed audio mod. This repository does not contain or modify it. Its source, binaries and license remain with its maintainers.

## Microsoft tools and APIs

The build uses a locally installed Windows SDK, Visual C++ toolchain and .NET Framework compiler. Those tools are not vendored. Runtime forwarding uses the operating system's own DirectInput library. The protection adapter reads a service identifier from the user's Windows ntdll at runtime; Windows library implementation bytes are not bundled. The DLL is built with the static C++ runtime. Toolchain and runtime terms remain separate from the project source license.

The startup file-index race and native menu layout were also cross-checked against [QualityOfLife.cpp](https://github.com/Lovrom8/RSMods/blob/bc2856bf99f0738cac8923cdf7b6e5aad2d55f4d/DLL/QualityOfLife.cpp) and [Offsets.cpp](https://github.com/Lovrom8/RSMods/blob/bc2856bf99f0738cac8923cdf7b6e5aad2d55f4d/DLL/Offsets.cpp) at the same pinned revision. The bounded request-capture fallback here is independently implemented; no RSMods source has been copied into it. No root LICENSE file was found in that upstream revision, so public source availability has not been treated as permission to redistribute its implementation. Compatibility detection hashes the separately installed image but never calls its internal functions.
