# Contributing

Build and run the offline checks in [docs/building.md](docs/building.md). Keep every native patch guarded by the exact supported image identity, expected instructions and calling convention. Unknown versions should retain the game's normal path for that hook.

Do not add personal profile strings, fixed Steam library paths, account identifiers, registry tweaks or automatic authentication. Preserve Steam lifetime behavior, Ubisoft connectivity, existing profiles, audio/display settings and the song library. Keep controller filtering optional.

For a behavior change, explain the trigger, resulting behavior, compatibility gates and relevant validation. Use native fixtures for stack/register/return-value assumptions. A trivial CALL/RET fixture can miss stack corruption; include realistic live frames where the ABI requires them. Offline checks do not replace gameplay validation.

Report crashes with the game and RSMods file hashes, selected startup options, the action that triggered the crash, and whether it reproduces with each option disabled. Include a short redacted log excerpt when useful. Do not attach complete diagnostic folders, profiles, game archives or memory dumps without reviewing them. The diagnostic tool can copy configurations and dumps into its output.

Do not contribute proprietary game assets, upstream binaries, credentials or private captures. Keep the public source folder separate from local evidence. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the existing external dependency boundary.
