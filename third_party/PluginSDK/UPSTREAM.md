# D2RLoader PluginSDK upstream pin

This directory contains the minimal build files and headers required to compile
Players HUD Slider from the official D2RLoader PluginSDK repository.

- Repository: https://github.com/D2RLoader/PluginSDK
- Tag: `v3`
- Commit: `4933e2c42cb2592958cd0df3b6dc5003102252d1`
- License: MIT; see `LICENSE`

The following files are copied byte-for-byte from that upstream commit:

- `include/D2RLPlugin/*.h`
- `cmake/D2RLPluginConfig.cmake`
- `cmake/D2RLPluginEmbedConfig.cmake`
- `cmake/D2RLPluginConfigResource.rc.in`
- `LICENSE`

`UPSTREAM-SHA256.txt` records the checksums for the copied upstream files.
The root project CMake file creates the SDK interface target and includes the
official embedded-config helper.