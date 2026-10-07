#include <D2RLPlugin/api.h>
#include <D2RLPlugin/logging.h>

#include "default_config.hpp"
#include "native_api.hpp"
#include "player_hud_slider.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#include <toml++/toml.hpp>

namespace {

using namespace players_hud_slider;

constexpr wchar_t ConfigFileName[] = L"players-hud-slider.toml";
constexpr std::int32_t MaximumPlayerCount = 65535;

using SetPlayerCountFn = void(__fastcall*)(void*, std::int32_t) noexcept;
using SetOfflineDifficultyRangeFn =
    void(__fastcall*)(void*, std::int32_t, std::int32_t) noexcept;
using GetOfflineDifficultySettingFn = void*(__fastcall*)() noexcept;
using GetCommandModeFn = std::int32_t(__fastcall*)() noexcept;
using CommitPlayersCommandFn = bool(__fastcall*)(std::int32_t) noexcept;

struct Settings {
    HudPlayerSliderConfig slider{};
};

const D2RL::PluginContext* Context{};
std::uintptr_t Base{};
Settings Config{};
std::int32_t MaximumPlayers{8};
std::atomic_bool Operational{};
HANDLE NativeOwner{};
SetPlayerCountFn RealSetPlayerCount{};
SetOfflineDifficultyRangeFn RealSetOfflineDifficultyRange{};
GetOfflineDifficultySettingFn RealGetOfflineDifficultySetting{};
GetCommandModeFn RealGetCommandMode{};
CommitPlayersCommandFn RealCommitPlayersCommand{};
PlayerHudSlider HudSlider{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "players-hud-slider",
    .name = "Players HUD Slider",
    .version = "1.1.15",
    .author = "Community",
    .description = "In-game /players slider attached to the HUD.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

template<class Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

auto IsReadableRange(std::uintptr_t address, std::size_t size) noexcept -> bool {
    if (address == 0 || size == 0
            || address > (std::numeric_limits<std::uintptr_t>::max)() - size) {
        return false;
    }
    const auto end = address + size;
    auto cursor = address;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory,
                sizeof(memory)) != sizeof(memory)
                || memory.State != MEM_COMMIT
                || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                || memory.RegionSize == 0) {
            return false;
        }
        const auto regionBase =
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (regionBase > (std::numeric_limits<std::uintptr_t>::max)()
                - memory.RegionSize) {
            return false;
        }
        const auto regionEnd = regionBase + memory.RegionSize;
        if (cursor < regionBase || cursor >= regionEnd) return false;
        cursor = (std::min)(end, regionEnd);
    }
    return true;
}

template<std::size_t Size>
auto Matches(std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected) noexcept -> bool {
    if (Base == 0
            || Base > (std::numeric_limits<std::uintptr_t>::max)() - rva) {
        return false;
    }
    const auto address = Base + rva;
    return IsReadableRange(address, expected.size())
        && std::memcmp(reinterpret_cast<const void*>(address),
            expected.data(), expected.size()) == 0;
}

auto ValidateNativeLayout() noexcept -> bool {
    const bool valid =
        Matches(native::ArtificialPlayerCountSetterRva,
            native::ArtificialPlayerCountSetterEntry)
        && Matches(native::PlayersCommandModeRva,
            native::PlayersCommandModeEntry)
        && Matches(native::PlayersCommandCommitRva,
            native::PlayersCommandCommitEntry)
        && Matches(native::PlayersCommandCommitCallRva,
            native::PlayersCommandCommitCall)
        && Matches(native::SetPlayerCountRva, native::SetPlayerCountEntry)
        && Matches(native::SetOfflineDifficultyRangeRva,
            native::SetOfflineDifficultyRangeEntry)
        && Matches(native::SetOfflineDifficultyRangeCallerRva,
            native::SetOfflineDifficultyRangeCaller)
        && Matches(native::OfflineDifficultyGetterRva,
            native::OfflineDifficultyGetterEntry)
        && Matches(native::OfflineDifficultyGetterReturnRva,
            native::OfflineDifficultyGetterReturn)
        && Matches(native::PlayersOfflineDifficultySetterContextRva,
            native::PlayersOfflineDifficultySetterContext);
    if (!valid && Context != nullptr) {
        Context->LogError(
            "Players HUD Slider: native signatures do not match the supported game layout; the plugin refused to load.");
    }
    return valid;
}

auto ConfigPath() -> std::filesystem::path {
    if (Context != nullptr && Context->pluginConfigPath != nullptr
            && Context->pluginConfigPath[0] != L'\0') {
        return std::filesystem::path(Context->pluginConfigPath);
    }
    std::error_code error;
    auto current = std::filesystem::current_path(error);
    if (error) return {};
    return current / L"d2rloader" / L"config" / ConfigFileName;
}

auto ReadInt(const toml::table& table, std::string_view key,
        std::int32_t minimum, std::int32_t maximum,
        std::int32_t& destination, std::string& error) -> bool {
    const auto* node = table.get(key);
    if (node == nullptr) return true;
    const auto value = node->value<std::int64_t>();
    if (!value || *value < minimum || *value > maximum) {
        error = std::string(key) + " is outside the supported range";
        return false;
    }
    destination = static_cast<std::int32_t>(*value);
    return true;
}

auto ReadBool(const toml::table& table, std::string_view key,
        bool& destination, std::string& error) -> bool {
    const auto* node = table.get(key);
    if (node == nullptr) return true;
    const auto value = node->value<bool>();
    if (!value) {
        error = std::string(key) + " must be true or false";
        return false;
    }
    destination = *value;
    return true;
}

auto ParseElement(const toml::table& parent, std::string_view name,
        HudElementConfig& element, std::int32_t minimumX,
        std::int32_t maximumX, std::int32_t minimumY,
        std::int32_t maximumY, std::int32_t minimumWidth,
        std::int32_t maximumWidth, std::int32_t minimumHeight,
        std::int32_t maximumHeight, std::string& error) -> bool {
    const auto* node = parent.get(name);
    if (node == nullptr) return true;
    const auto* table = node->as_table();
    if (table == nullptr) {
        error = std::string(name) + " must be a table";
        return false;
    }
    return ReadInt(*table, "x", minimumX, maximumX, element.x, error)
        && ReadInt(*table, "y", minimumY, maximumY, element.y, error)
        && ReadInt(*table, "width", minimumWidth, maximumWidth,
            element.width, error)
        && ReadInt(*table, "height", minimumHeight, maximumHeight,
            element.height, error);
}

auto ParseConfig(std::string_view text, Settings& destination,
        std::string& error) -> bool {
    try {
        const auto root = toml::parse(text);
        const auto* sectionNode = root.get("hud-player-slider");
        if (sectionNode == nullptr) return true;
        const auto* section = sectionNode->as_table();
        if (section == nullptr) {
            error = "hud-player-slider must be a TOML table";
            return false;
        }
        if (!ReadBool(*section, "enabled", destination.slider.enabled, error)
                || !ReadInt(*section, "state-max-fallback", 1,
                    MaximumPlayerCount, destination.slider.stateMaxFallback,
                    error)) {
            return false;
        }
        return ParseElement(*section, "button", destination.slider.button,
                0, 3200, 0, 1801, 8, 800, 8, 800, error)
            && ParseElement(*section, "track", destination.slider.track,
                0, 3200, 0, 1801, 32, 3200, 4, 500, error)
            && ParseElement(*section, "marker", destination.slider.marker,
                0, 3200, 0, 1801, 8, 500, 8, 500, error)
            && ParseElement(*section, "tooltip", destination.slider.tooltip,
                -1000, 1000, -1000, 1000, 16, 800, 16, 500, error)
            && ParseElement(*section, "tooltip-text",
                destination.slider.tooltipText, -1000, 1000, -1000, 1000,
                8, 800, 8, 500, error)
            && ParseElement(*section, "tick", destination.slider.tick,
                -1000, 1000, -1000, 1000, 1, 40, 1, 100, error);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

auto LoadConfig() noexcept -> bool {
    Config = {};
    auto text = std::string_view(DefaultConfig);
    try {
        const auto path = ConfigPath();
        if (!path.empty()) {
            std::error_code error;
            if (!std::filesystem::exists(path, error) && !error) {
                std::filesystem::create_directories(path.parent_path(), error);
                if (!error) {
                    std::ofstream output(path, std::ios::binary);
                    if (output.is_open()) {
                        output.write(DefaultConfig,
                            static_cast<std::streamsize>(
                                sizeof(DefaultConfig) - 1));
                    }
                }
            }
            error.clear();
            if (std::filesystem::is_regular_file(path, error) && !error) {
                std::ifstream input(path, std::ios::binary);
                if (!input.is_open()) {
                    Context->LogWarn(
                        "Players HUD Slider: config could not be opened; embedded defaults will be used.");
                } else {
                    const std::string contents{
                        std::istreambuf_iterator<char>(input),
                        std::istreambuf_iterator<char>()};
                    static std::string configStorage;
                    configStorage = contents;
                    text = configStorage;
                }
            }
        }
    } catch (...) {
        Context->LogWarn(
            "Players HUD Slider: config access failed; embedded defaults will be used.");
    }

    std::string error;
    if (!ParseConfig(text, Config, error)) {
        D2RL::LogErrorF(Context, "Players HUD Slider: invalid TOML config: %s",
            error.c_str());
        return false;
    }
    return true;
}

auto AcquireNativeOwner() noexcept -> bool {
    wchar_t name[128]{};
    if (swprintf_s(name,
            L"Local\\RuffnecKk.PlayerXScalingTweaks.NativeOwner.v1.%lu",
            static_cast<unsigned long>(GetCurrentProcessId())) < 0) {
        return false;
    }
    NativeOwner = CreateMutexW(nullptr, FALSE, name);
    if (NativeOwner == nullptr) return false;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return true;
    CloseHandle(NativeOwner);
    NativeOwner = nullptr;
    Context->LogError(
        "Players HUD Slider: another plugin already owns the Offline Difficulty player-count setting.");
    return false;
}

void ReleaseNativeOwner() noexcept {
    if (NativeOwner != nullptr) {
        CloseHandle(NativeOwner);
        NativeOwner = nullptr;
    }
}

auto AllowsPlayersMode(std::int32_t mode) noexcept -> bool {
    return mode == 0 || mode == 5 || mode == 7;
}

auto CanApplyPlayers() noexcept -> bool {
    return Operational.load(std::memory_order_acquire)
        && RealGetCommandMode != nullptr
        && AllowsPlayersMode(RealGetCommandMode());
}

auto PreparePlayersRange() noexcept -> bool {
    if (!CanApplyPlayers() || RealGetOfflineDifficultySetting == nullptr
            || RealSetOfflineDifficultyRange == nullptr) {
        return false;
    }
    auto* setting = RealGetOfflineDifficultySetting();
    if (setting == nullptr) return false;
    RealSetOfflineDifficultyRange(setting, 1, MaximumPlayers);
    return true;
}

auto ApplyPlayers(std::int32_t players) noexcept -> bool {
    if (!CanApplyPlayers() || RealGetOfflineDifficultySetting == nullptr
            || RealSetOfflineDifficultyRange == nullptr
            || RealSetPlayerCount == nullptr
            || RealCommitPlayersCommand == nullptr) {
        return false;
    }
    const auto chosen = std::clamp(players, 1, MaximumPlayers);
    auto* setting = RealGetOfflineDifficultySetting();
    if (setting == nullptr) return false;

    RealSetOfflineDifficultyRange(setting, 1, MaximumPlayers);
    RealSetPlayerCount(setting, chosen);
    (void)RealCommitPlayersCommand(0);
    HudSlider.SetCurrentPlayers(chosen);

    char message[96]{};
    std::snprintf(message, sizeof(message),
        "Players HUD Slider: /players %d applied.", chosen);
    Context->WriteConsoleMessage(message);
    return true;
}

void ResetPluginState() noexcept {
    Operational.store(false, std::memory_order_release);
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)
            || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = context->exeBase;
    ResetPluginState();
    if (!LoadConfig()) return false;
    if (!Config.slider.enabled) {
        Context->LogInfo(
            "Players HUD Slider loaded disabled by config.");
        return true;
    }

    MaximumPlayers = ReadLoaderMaximumPlayers(
        context, Config.slider.stateMaxFallback);
    if (!AcquireNativeOwner() || !ValidateNativeLayout()) {
        ReleaseNativeOwner();
        return false;
    }
    RealSetPlayerCount = At<SetPlayerCountFn>(
        native::SetPlayerCountRva);
    RealSetOfflineDifficultyRange = At<SetOfflineDifficultyRangeFn>(
        native::SetOfflineDifficultyRangeRva);
    RealGetOfflineDifficultySetting = At<GetOfflineDifficultySettingFn>(
        native::OfflineDifficultyGetterRva);
    RealGetCommandMode = At<GetCommandModeFn>(
        native::PlayersCommandModeRva);
    RealCommitPlayersCommand = At<CommitPlayersCommandFn>(
        native::PlayersCommandCommitRva);

    if (MaximumPlayers > 8
            && !Context->PatchBytes(
                native::ArtificialPlayerCountLimitRva,
                native::ArtificialPlayerCountLimitOriginal.data(),
                static_cast<std::uint32_t>(
                    native::ArtificialPlayerCountLimitOriginal.size()),
                native::ArtificialPlayerCountLimitPatched.data(),
                static_cast<std::uint32_t>(
                    native::ArtificialPlayerCountLimitPatched.size()))) {
        Context->LogError(
            "Players HUD Slider: could not reserve the native player-count extension patch.");
        ReleaseNativeOwner();
        return false;
    }

    Operational.store(true, std::memory_order_release);
    if (!HudSlider.Start(context, Config.slider, MaximumPlayers,
            CanApplyPlayers, PreparePlayersRange, ApplyPlayers)) {
        Operational.store(false, std::memory_order_release);
        ReleaseNativeOwner();
        return false;
    }

    D2RL::LogInfoF(Context,
        "Players HUD Slider 1.1.15 active; maximum players=%d.",
        MaximumPlayers);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Operational.store(false, std::memory_order_release);
    HudSlider.Stop();
    RealSetPlayerCount = nullptr;
    RealSetOfflineDifficultyRange = nullptr;
    RealGetOfflineDifficultySetting = nullptr;
    RealGetCommandMode = nullptr;
    RealCommitPlayersCommand = nullptr;
    ReleaseNativeOwner();
    Context = nullptr;
    Base = 0;
}
