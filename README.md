# Players HUD Slider

Players HUD Slider is a standalone D2RLoader plugin for changing the Offline
`/players` setting from the HUD. Click its diamond button to open the slider,
drag to a player count, then release to apply the value and close the slider.
The slider ranges from 1 to `maxPlayers` in `d2rloader/data/state.json`.

It includes the source artwork and generated native `.sprite` resources.
Its CMake configure step regenerates the native sprites from the PNG artwork.
The prebuilt DLL is in [`dist`](dist/), and the loader config template is in
[`config`](config/).

## Install

Copy `dist/d2rl-players-hud-slider.dll` to:

```text
mods/<mod name>/d2rloader/plugins/d2rl-players-hud-slider.dll
```

On first launch, D2RLoader creates
`d2rloader/config/players-hud-slider.toml`. All HUD element positions and
sizes can be adjusted there. If `state.json` has no usable `maxPlayers`, the
plugin uses `state-max-fallback` from the TOML file.

Do not enable this plugin alongside PlayerX Scaling Tweaks. Both plugins use
the same native Offline Difficulty setting; a shared owner mutex prevents
them from competing.

## Build

Build on Windows x64 with CMake 3.28+, Visual Studio 2022 or later, the Windows
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

The plugin targets D2RLoader ABI 4 and was built for the D2R 3.3.93847
reference layout. It validates native signatures and leaves an unknown game
layout untouched.

The v1.1.16 DLL SHA-256 is recorded in
[`dist/SHA256SUMS.txt`](dist/SHA256SUMS.txt).

## Assets

`assets/artwork/` contains the custom PNG source sprites. The approved button
state sheet is ordered normal, hover, pressed, disabled. The build script
aligns those states and generates `assets/native/*.sprite`, which are also
included in the repository.

See [third-party notices](THIRD_PARTY_NOTICES.md) for SDK and dependency
licenses.
