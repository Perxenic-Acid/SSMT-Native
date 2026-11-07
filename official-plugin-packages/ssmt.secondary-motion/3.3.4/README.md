# Secondary Motion SSMT package

This package integrates the upstream `sbm.dll` runtime as an SSMT plugin.

This is an SSMT adaptation based on Sp1cHless's original project and is not an
official release of that project. It is maintained by a third party on a
best-effort basis, with no guarantee of continued updates or timely
compatibility with future game versions. Please do not ask the original
project's author to expedite updates or report issues with this adaptation to
them.

- The DLL is loaded through SSMT's direct extra-DLL injection path.
- `plugin/sbm.dll` remains inside the installed package.
- Persistent presets live in SSMT's plugin-scoped `SecondaryMotion/` data directory.
- The host mirrors persistent data into the package-relative `SecondaryMotion/` runtime directory before launch.
- No proxy DLL or runtime data is copied to the game root.
- The marketplace plugin's enable switch controls whether SSMT injects the DLL.
- The package-owned static settings UI controls the runtime bone-write switch, preset, and character parameters through Plugin API 1.

The pinned upstream source and license are recorded in `SOURCE.json`, `LICENSE`
and `THIRD_PARTY_NOTICES.md`. The package is provided as-is without any express
or implied warranty. Native injection may trigger anti-cheat or account risk
controls, cause crashes or save-related issues, and may stop working after a
game update.
