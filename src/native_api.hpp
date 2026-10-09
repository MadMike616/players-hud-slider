#pragma once
#include <array>
#include <cstdint>

namespace players_hud_slider::native {

// These RVAs are retained from the plugin's original D2R reference layout.
inline constexpr std::uintptr_t ArtificialPlayerCountLimitRva = 0x425A69;
inline constexpr std::array<std::uint8_t, 3> ArtificialPlayerCountLimitOriginal{
    0x0F, 0x46, 0xC1};
inline constexpr std::array<std::uint8_t, 3> ArtificialPlayerCountLimitPatched{
    0x89, 0xC8, 0x90};

inline constexpr std::uintptr_t PlayersCommandModeRva = 0x8AF90;
inline constexpr std::uintptr_t PlayersCommandCommitRva = 0xA07670;
inline constexpr std::uintptr_t SetPlayerCountRva = 0xD2F020;
inline constexpr std::uintptr_t SetOfflineDifficultyRangeRva = 0xD2ED90;
inline constexpr std::uintptr_t OfflineDifficultyGetterRva = 0xE19E0;

} // namespace players_hud_slider::native
