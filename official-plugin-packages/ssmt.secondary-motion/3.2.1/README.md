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
- Runtime configuration stays in the sibling `SecondaryMotion/` directory.
- No proxy DLL or runtime data is copied to the game root.
- The marketplace plugin's enable switch controls whether SSMT injects the DLL.
- The plugin settings page controls the runtime bone-write switch and preset.

The pinned upstream source and license are recorded in `SOURCE.json`, `LICENSE`
and `THIRD_PARTY_NOTICES.md`. The package is provided as-is without any express
or implied warranty. Native injection may trigger anti-cheat or account risk
controls, cause crashes or save-related issues, and may stop working after a
game update.
