# Hook Inventory — Players HUD Slider

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Players HUD Slider |
| Plugin ID | players-hud-slider |
| Release version | 1.1.18 |
| Inventory updated | 2026-11-08 |
| Source revision | Based on 077134c8be4f9a84c09f74febd2900f2fd67eb3c; working tree modified for v1.1.18 |
| D2R build(s) audited | D2R 3.3.93847 reference layout per README; not revalidated in-game for this inventory |
| D2RLoader SDK / Plugin ABI | PluginSDK 0.4.0 / Plugin ABI 4 |
| Audit status | Partial |

## Scope and method

Inspected src/plugin.cpp, src/native_api.hpp, src/player_hud_slider.cpp, src/plugin.rc, README.md, CMakeLists.txt, and the vendored SDK ABI headers. The plugin patches one native byte sequence and subclasses its own timer-window procedure. It does not install a native inline detour.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|
| H-01 | byte patch | Artificial player-count limit | RVA 0x425A69 | PatchBytes replaces the original three-byte limit with the configured extended limit | SDK PatchBytes checks the expected original bytes before writing | src/plugin.cpp — D2RLoaderLoadPlugin; src/native_api.hpp — ArtificialPlayerCountLimitOriginal | Applied only when the configured maximum exceeds 8. |
| H-02 | Win32 input-message | Plugin-owned timer window procedure | Dynamic: timerWindow_ | SetWindowLongPtrW replaces GWLP_WNDPROC with the slider timer procedure | The plugin creates and owns timerWindow_; the previous procedure is saved and restored on shutdown | src/player_hud_slider.cpp — Start / Stop | Private window subclass, not a D2R main-window hook. |

## Known collision overlaps

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|
| RuffDood PlayerX Scaling Tweaks | ArtificialPlayerCountLimitRva, RVA 0x425A69 | Same patch site | The workspace aggregate lists this plugin patching the same player-count limit. Compare expected bytes before enabling both. |

## Audit notes

Version 1.1.18 upgrades to PluginSDK 0.4.0 / ABI 4 and has no hook-site changes from v1.1.17. The README notes that v1.1.17 removed the separate exact-byte startup signature check. The patch still uses the SDK expected-byte PatchBytes contract. Source-only review; no in-game validation was performed for this inventory.
