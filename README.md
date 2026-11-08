# Players HUD Slider

Players HUD Slider is a standalone D2RLoader plugin for changing the Offline
`/players` setting from the HUD. Click its diamond button to open the slider,
drag to a player count, then release to apply the value and close the slider.
The slider ranges from 1 to `maxPlayers` in `d2rloader/data/state.json`.

The plugin reports `MadMike` as its author in D2RLoader. It includes the source artwork and generated native `.sprite` resources, including lower-resolution `.lowend.sprite` variants for D2R's low-end graphics mode. All sprite resources are embedded in the DLL; no sprite sidecar files are needed at runtime.
Its CMake configure step regenerates the standard native sprites from the PNG artwork; the lowend variants are included as source resources.
The prebuilt DLL is in [`dist`](dist/), and the loader config template is in
[`config`](config/).

## Install

Copy `dist/d2rl-players-hud-slider.dll` to the plugin folder for your D2RLoader mod:

```text
mods/<mod>/d2rloader/plugins/d2rl-players-hud-slider.dll
```

On first launch, D2RLoader creates
`d2rloader/config/players-hud-slider.toml`. All HUD element positions and
sizes can be adjusted there. If `state.json` has no usable `maxPlayers`, the
plugin uses `state-max-fallback` from the TOML file.

Do not enable this plugin alongside PlayerX Scaling Tweaks. Both plugins use
the same native Offline Difficulty setting; a shared owner mutex prevents
them from competing.

## Build

Build on Windows x64 with CMake 3.29+, Visual Studio 2022 or later, the Windows
SDK, and PowerShell. The first CMake configure downloads toml++ v3.4.0. The
minimal D2RLoader PluginSDK build surface is included under
`third_party/PluginSDK` with its upstream license and provenance.

```powershell
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target players_hud_slider
```

Run those commands from an x64 Visual Studio Developer Command Prompt. The
Release DLL is `build/d2rl-players-hud-slider.dll`.

## Compatibility

The plugin targets D2RLoader Plugin ABI 4 and requires a loader with ABI 4
support. It uses native addresses from the D2R 3.3.93847 reference layout.
Version 1.1.17 removed the exact-byte startup
signature check. Version 1.1.18 adds lowend sprite resources while keeping the
existing fixed addresses, so other game layouts are not automatically
supported.

The v1.1.18 DLL SHA-256 is recorded in
[`dist/SHA256SUMS.txt`](dist/SHA256SUMS.txt).

## Assets

`assets/artwork/` contains the custom PNG source sprites. The approved button
state sheet is ordered normal, hover, pressed, disabled. The build script
aligns those states and generates `assets/native/*.sprite`, which are also
included in the repository.

YupGoolg discovered D2R's low-end sprite lookup behavior and provided the
lowend sprite solution used by this plugin.

See [third-party notices](THIRD_PARTY_NOTICES.md) for SDK and dependency
licenses.
