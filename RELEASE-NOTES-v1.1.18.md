# Players HUD Slider v1.1.18

- Adds lower-resolution sprite resources for the HUD button, slider track, marker, and tooltip. D2R can load these `.lowend.sprite` variants when low-end graphics resources are selected. YupGoolg discovered this lookup behavior and provided the lowend sprite solution used here.
- Keeps the v1.1.17 startup behavior and fixed native addresses.
- Upgrades the vendored v3 SDK / Plugin ABI 3 to SDK 0.4.0 / Plugin ABI 4.
- Publishes this DLL from the Release configuration.

## Compatibility

- D2RLoader Plugin ABI 4 required
- Addresses remain based on the D2R 3.3.93847 reference layout.
- Install the DLL in `mods/<mod>/d2rloader/plugins/`.

## SHA-256

`C222C3A41DC935142B29E3ED62B4E1C77E14AB719C6DC4A53FD2455A6A4D6783`
