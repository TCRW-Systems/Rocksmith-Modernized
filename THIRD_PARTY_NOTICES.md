# External dependencies and acknowledgments

## Rocksmith 2014 Remastered

Rocksmith and associated names belong to their respective owners. A separately obtained game installation is required. This repository contains no game executable, DLC, audio, graphics, game archives, profiles, crash dumps or extracted code sections. Short instruction signatures and image hashes in the source identify supported hook targets. This project is not affiliated with Ubisoft.

## RSMods

[RSMods](https://github.com/Lovrom8/RSMods) is a separate runtime dependency for the native classifier, intro and profile adapters. The currently pinned revision is [`bc2856bf99f0738cac8923cdf7b6e5aad2d55f4d`](https://github.com/Lovrom8/RSMods/tree/bc2856bf99f0738cac8923cdf7b6e5aad2d55f4d).

The adapters call the existing loaded memory-protection helper and profile tick through a verified build-specific ABI. The project does not bundle RSMods source, binaries, symbols, GUI resources, Fast Load assets or its third-party libraries. RSMods' existing song-scan improvements remain part of RSMods. Relevant upstream implementation: [MemUtil.cpp](https://github.com/Lovrom8/RSMods/blob/bc2856bf99f0738cac8923cdf7b6e5aad2d55f4d/DLL/MemUtil.cpp).

Do not infer permission to redistribute an upstream project from its public availability. Obtain RSMods from its maintainers and review its terms separately before proposing bundled distributions. This repository's MIT license does not grant rights to RSMods or game content.

## RS ASIO

[RS ASIO](https://github.com/mdias/rs_asio) is an optional, separately installed audio mod. This repository does not contain or modify it. Its source, binaries and license remain with its maintainers.

## Microsoft tools and APIs

The build uses a locally installed Windows SDK, Visual C++ toolchain and .NET Framework compiler. Those tools are not vendored. Runtime forwarding uses the operating system's own DirectInput library. The DLL is built with the static C++ runtime. Toolchain and runtime terms remain separate from the project source license.
