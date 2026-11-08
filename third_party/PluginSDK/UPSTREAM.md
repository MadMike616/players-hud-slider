# D2RLoader PluginSDK upstream pin

This directory vendors the SDK headers and CMake helpers required to compile
Players HUD Slider from the official D2RLoader PluginSDK repository.

- Repository: https://github.com/D2RLoader/PluginSDK
- SDK release: 0.4.0
- Plugin ABI: 4
- Source ref: `master`
- Commit: `03a5af9e0feb3f95f95e148e330e01f6635950df`
- License: MIT; see `LICENSE`

All files under `include/D2RLPlugin/`, the three CMake resource/configuration
helpers under `cmake/`, and `LICENSE` are copied from that upstream commit.

`UPSTREAM-SHA256.txt` records checksums for the copied upstream files.
The root CMake file creates the SDK interface target and includes the official
embedded-config helper.
