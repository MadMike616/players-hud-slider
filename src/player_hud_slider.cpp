#include "player_hud_slider.hpp"

#include <D2RLPlugin/logging.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace players_hud_slider {
namespace {

constexpr wchar_t TimerWindowClassName[] = L"STATIC";
constexpr std::int32_t MaximumHudTicks = 32;

struct HostWindowCandidate {
    HWND window{};
    long long area{};
    bool hasGameTitle{};
};

BOOL CALLBACK FindHostWindowCallback(HWND window, LPARAM parameter) noexcept {
    auto* candidate = reinterpret_cast<HostWindowCandidate*>(parameter);
    DWORD processId{};
    (void)GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId() || !IsWindowVisible(window)
            || IsIconic(window)) {
        return TRUE;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) return TRUE;
    const long long width = client.right - client.left;
    const long long height = client.bottom - client.top;
    const long long area = width * height;
    if (area < 640LL * 480LL) return TRUE;

    wchar_t title[256]{};
    (void)GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    const std::wstring_view titleView(title);
    const bool gameTitle = titleView.find(L"Diablo") != std::wstring_view::npos
        || titleView.find(L"D2R") != std::wstring_view::npos;
    if ((gameTitle && !candidate->hasGameTitle)
            || (gameTitle == candidate->hasGameTitle && area > candidate->area)) {
        candidate->window = window;
        candidate->area = area;
        candidate->hasGameTitle = gameTitle;
    }
    return TRUE;
}

auto FindHostWindow() noexcept -> HWND {
    HWND foreground = GetForegroundWindow();
    DWORD processId{};
    (void)GetWindowThreadProcessId(foreground, &processId);
    if (processId == GetCurrentProcessId() && IsWindowVisible(foreground)) {
        RECT client{};
        if (GetClientRect(foreground, &client)
                && client.right - client.left >= 640
                && client.bottom - client.top >= 480) {
            return foreground;
        }
    }
    HostWindowCandidate candidate{};
    (void)EnumWindows(FindHostWindowCallback,
        reinterpret_cast<LPARAM>(&candidate));
    return candidate.window;
}

auto AddStateCandidate(
        std::vector<std::filesystem::path>& candidates,
        const std::filesystem::path& base) -> void {
    if (base.empty()) return;
    const auto normalized = base.lexically_normal();
    const auto candidate = normalized.filename() == L"d2rloader"
        ? normalized / L"data" / L"state.json"
        : normalized / L"d2rloader" / L"data" / L"state.json";
    if (std::find(candidates.begin(), candidates.end(), candidate)
            == candidates.end()) {
        candidates.push_back(candidate);
    }
}

auto ParseLoaderMaxPlayers(std::string_view json, std::int32_t& players)
        noexcept -> bool {
    const auto multiplayer = json.find("\"multiplayer\"");
    if (multiplayer == std::string_view::npos) return false;
    const auto objectStart = json.find('{', multiplayer);
    if (objectStart == std::string_view::npos) return false;

    std::size_t objectEnd = objectStart + 1;
    int depth = 1;
    bool inString{};
    bool escaped{};
    for (; objectEnd < json.size() && depth > 0; ++objectEnd) {
        const char c = json[objectEnd];
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '{') ++depth;
        else if (c == '}') --depth;
    }
    if (depth != 0) return false;

    const auto object = json.substr(objectStart, objectEnd - objectStart);
    const auto property = object.find("\"maxPlayers\"");
    if (property == std::string_view::npos) return false;
    const auto colon = object.find(':', property + sizeof("\"maxPlayers\"") - 1);
    if (colon == std::string_view::npos) return false;
    auto cursor = colon + 1;
    while (cursor < object.size()
            && std::isspace(static_cast<unsigned char>(object[cursor]))) {
        ++cursor;
    }
    const auto start = cursor;
    while (cursor < object.size() && object[cursor] >= '0'
            && object[cursor] <= '9') {
        ++cursor;
    }
    if (start == cursor) return false;
    std::int32_t parsed{};
    const auto result = std::from_chars(object.data() + start,
        object.data() + cursor, parsed);
    if (result.ec != std::errc{} || result.ptr != object.data() + cursor
            || parsed < 1 || parsed > MaximumHudPlayerCount) {
        return false;
    }
    players = parsed;
    return true;
}

auto ReadStateFile(
        const std::filesystem::path& path,
        std::int32_t& players) -> bool {
    std::error_code statusError;
    if (!std::filesystem::is_regular_file(path, statusError) || statusError) {
        return false;
    }
    const auto fileSize = std::filesystem::file_size(path, statusError);
    if (statusError || fileSize == 0 || fileSize > 1024 * 1024) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return false;
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return ParseLoaderMaxPlayers(json, players);
}

auto RectContains(
        POINT point,
        std::int32_t x,
        std::int32_t y,
        std::int32_t width,
        std::int32_t height) noexcept -> bool {
    return point.x >= x && point.y >= y
        && point.x < x + width && point.y < y + height;
}

} // namespace

auto ReadLoaderMaximumPlayers(
        const D2RL::PluginContext* context,
        std::int32_t fallback) noexcept -> std::int32_t {
    std::vector<std::filesystem::path> candidates;
    try {
        if (context != nullptr && context->scopeRootDirectory != nullptr
                && context->scopeRootDirectory[0] != L'\0') {
            AddStateCandidate(candidates,
                std::filesystem::path(context->scopeRootDirectory));
        }
        if (context != nullptr && context->pluginConfigPath != nullptr
                && context->pluginConfigPath[0] != L'\0') {
            auto parent = std::filesystem::path(context->pluginConfigPath)
                .parent_path();
            for (int depth = 0; depth < 6 && !parent.empty(); ++depth) {
                AddStateCandidate(candidates, parent);
                const auto next = parent.parent_path();
                if (next == parent) break;
                parent = next;
            }
        }
        std::error_code currentPathError;
        const auto currentPath = std::filesystem::current_path(currentPathError);
        if (!currentPathError) AddStateCandidate(candidates, currentPath);

        std::array<wchar_t, 32768> executablePath{};
        const auto pathLength = GetModuleFileNameW(nullptr,
            executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (pathLength > 0 && pathLength < executablePath.size()) {
            AddStateCandidate(candidates,
                std::filesystem::path(executablePath.data()).parent_path());
        }

        for (const auto& candidate : candidates) {
            std::int32_t parsed{};
            if (ReadStateFile(candidate, parsed)) {
                D2RL::LogInfoF(context,
                    "Players HUD slider: using maxPlayers=%d from %s.",
                    parsed, candidate.string().c_str());
                return parsed;
            }
        }
    } catch (...) {
        // Fall through to the configured fallback. This file is read-only input.
    }

    const auto safeFallback = std::clamp(
        fallback, 1, MaximumHudPlayerCount);
    D2RL::LogWarnF(context,
        "Players HUD slider: could not read d2rloader/data/state.json maxPlayers; using fallback=%d.",
        safeFallback);
    return safeFallback;
}

auto PlayerHudSlider::Start(
        const D2RL::PluginContext* context,
        const HudPlayerSliderConfig& config,
        std::int32_t maximumPlayers,
        CanApply canApply,
        Prepare prepare,
        Apply apply) noexcept -> bool {
    Stop();
    if (context == nullptr || !config.enabled) return true;
    context_ = context;
    config_ = config;
    maximumPlayers_ = std::clamp(
        maximumPlayers, 1, MaximumHudPlayerCount);
    canApply_ = canApply;
    prepare_ = prepare;
    apply_ = apply;
    selectedPlayers_ = currentPlayers_.load(std::memory_order_acquire);
    active_.store(true, std::memory_order_release);
    if (!RegisterListeners()
            || !nativePanel_.Start(context_, config_, maximumPlayers_,
                OnNativeButtonClick, this)) {
        Stop();
        return false;
    }
    return true;
}

void PlayerHudSlider::Stop() noexcept {
    active_.store(false, std::memory_order_release);
    sessionReady_ = false;
    DestroyInteractionTimer();
    nativePanel_.Stop();
    if (context_ != nullptr && lifecycle_ != nullptr
            && lifecycle_->unregisterGameplayEventListener != nullptr) {
        for (auto& listener : listeners_) {
            if (listener != D2RL::Lifecycle::InvalidHandle) {
                (void)lifecycle_->unregisterGameplayEventListener(context_, listener);
                listener = D2RL::Lifecycle::InvalidHandle;
            }
        }
    }
    lifecycle_ = nullptr;
    context_ = nullptr;
    canApply_ = nullptr;
    prepare_ = nullptr;
    apply_ = nullptr;
}

void PlayerHudSlider::SetCurrentPlayers(std::int32_t players) noexcept {
    const auto clamped = std::clamp(players, 1, maximumPlayers_);
    currentPlayers_.store(clamped, std::memory_order_release);
}

auto PlayerHudSlider::RegisterListeners() noexcept -> bool {
    if (context_->QueryService(D2RL::ServiceId::Lifecycle,
            D2RL::LifecycleServiceV1Version, &lifecycle_)
            != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceV1Field(lifecycle_,
                D2RL::LifecycleServiceV1RequiredSize)
            || lifecycle_->registerGameplayEventListener == nullptr
            || lifecycle_->unregisterGameplayEventListener == nullptr) {
        lifecycle_ = nullptr;
        context_->LogWarn(
            "Players HUD slider: LifecycleService v1 is unavailable.");
        return false;
    }

    constexpr std::array kinds{
        D2RL::Lifecycle::GameplayEventKind::GameJoined,
        D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
        D2RL::Lifecycle::GameplayEventKind::GameLeft,
    };
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const D2RL::Lifecycle::GameplayEventListener listener{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .flags = 0,
            .kind = kinds[i],
            .reserved = 0,
            .callback = OnGameplay,
            .userData = this,
        };
        if (lifecycle_->registerGameplayEventListener(context_, &listener,
                &listeners_[i]) != D2RL::Lifecycle::Result::Success
                || listeners_[i] == D2RL::Lifecycle::InvalidHandle) {
            context_->LogWarn(
                "Players HUD slider: could not register gameplay lifecycle events.");
            return false;
        }
    }
    return true;
}

void __cdecl PlayerHudSlider::OnGameplay(
        const D2RL::PluginContext* context,
        const D2RL::Lifecycle::GameplayEvent* event,
        void* userData) noexcept {
    if (context == nullptr || event == nullptr || userData == nullptr
            || event->structSize < D2RL::Lifecycle::GameplayEventRequiredSize) {
        return;
    }
    auto& self = *static_cast<PlayerHudSlider*>(userData);
    if (context != self.context_ || !self.active_.load(std::memory_order_acquire)) {
        return;
    }
    if (event->kind == D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady) {
        self.OnSessionReady();
    } else if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameJoined
            || event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) {
        self.OnSessionEnded();
    }
}

void PlayerHudSlider::OnSessionReady() noexcept {
    if (!active_.load(std::memory_order_acquire)) return;
    sessionReady_ = true;
    nativePanel_.Close();
    sliderOpen_ = false;
    nativeDragActive_ = false;
    selectedPlayers_ = std::clamp(
        currentPlayers_.load(std::memory_order_acquire), 1, maximumPlayers_);
    if (canApply_ != nullptr && !canApply_()) return;
    if (!CreateInteractionTimer()) {
        if (context_ != nullptr) {
            context_->LogWarn(
                "Players HUD slider: could not create its interaction timer.");
        }
        return;
    }
    nativePanel_.ShowHudButton();
}

void PlayerHudSlider::OnSessionEnded() noexcept {
    sessionReady_ = false;
    nativePanel_.Close();
    nativePanel_.HideHudButton();
    sliderOpen_ = false;
    nativeDragActive_ = false;
    DestroyInteractionTimer();
}

void __cdecl PlayerHudSlider::OnNativeButtonClick(void* userData) noexcept {
    auto* self = static_cast<PlayerHudSlider*>(userData);
    if (self != nullptr && self->active_.load(std::memory_order_acquire)
            && self->sessionReady_) {
        self->ToggleSlider();
    }
}

auto PlayerHudSlider::CreateInteractionTimer() noexcept -> bool {
    if (timerWindow_ != nullptr && IsWindow(timerWindow_)) return true;
    hostWindow_ = FindHostWindow();
    if (hostWindow_ == nullptr) return false;

    timerWindow_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        TimerWindowClassName, L"", WS_POPUP, 0, 0, 1, 1,
        hostWindow_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (timerWindow_ == nullptr) {
        hostWindow_ = nullptr;
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    previousWindowProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        timerWindow_, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(WindowProc)));
    if (previousWindowProc_ == nullptr && GetLastError() != ERROR_SUCCESS) {
        (void)DestroyWindow(timerWindow_);
        timerWindow_ = nullptr;
        hostWindow_ = nullptr;
        return false;
    }
    (void)SetWindowLongPtrW(timerWindow_, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(this));
    if (SetTimer(timerWindow_, RefreshTimerId, 250, nullptr) == 0
            || SetTimer(timerWindow_, NativeDragTimerId, 10, nullptr) == 0) {
        DestroyInteractionTimer();
        return false;
    }
    return true;
}

void PlayerHudSlider::DestroyInteractionTimer() noexcept {
    const HWND window = timerWindow_;
    if (window != nullptr && IsWindow(window)) {
        if (GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId()) {
            (void)SendMessageW(window, DetachMessage, 0, 0);
            if (IsWindow(window)) (void)DestroyWindow(window);
        } else {
            (void)SendMessageW(window, DetachMessage, 0, 0);
            if (IsWindow(window)) (void)PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }
    timerWindow_ = nullptr;
    previousWindowProc_ = nullptr;
    hostWindow_ = nullptr;
}

void PlayerHudSlider::ToggleSlider() noexcept {
    if (nativePanel_.IsOpen()) {
        nativePanel_.Close();
        sliderOpen_ = false;
        nativeDragActive_ = false;
    } else if (canApply_ != nullptr && canApply_()
            && prepare_ != nullptr && prepare_()
            && nativePanel_.Open()) {
        sliderOpen_ = true;
        selectedPlayers_ = std::clamp(
            currentPlayers_.load(std::memory_order_acquire), 1, maximumPlayers_);
        nativeDragActive_ = false;
    } else {
        sliderOpen_ = false;
        nativeDragActive_ = false;
        if (context_ != nullptr) {
            context_->LogWarn(
                "Players HUD Slider: the native player-count panel could not be opened.");
        }
    }
}

void PlayerHudSlider::PollNativeSliderDrag() noexcept {
    if (!sliderOpen_) return;
    if (!nativePanel_.IsOpen()) {
        sliderOpen_ = false;
        nativeDragActive_ = false;
        return;
    }
    if (canApply_ == nullptr || !canApply_()) {
        nativePanel_.Close();
        sliderOpen_ = false;
        nativeDragActive_ = false;
        return;
    }

    const bool leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    POINT screenPoint{};
    POINT logicalPoint{};
    const bool pointerReady = GetCursorPos(&screenPoint)
        && LogicalPoint(screenPoint, logicalPoint);
    if (leftButtonDown) {
        if (!nativeDragActive_ && pointerReady
                && IsSliderPoint(logicalPoint)) {
            nativeDragActive_ = true;
            selectedPlayers_ = ResolvePlayerCount(logicalPoint);
            nativePanel_.QueueTooltipValue(selectedPlayers_);
        }
        if (nativeDragActive_ && pointerReady) {
            const auto selected = ResolvePlayerCount(logicalPoint);
            if (selected != selectedPlayers_) {
                selectedPlayers_ = selected;
                nativePanel_.QueueTooltipValue(selectedPlayers_);
            }
        }
        return;
    }
    if (!nativeDragActive_) return;

    if (pointerReady) selectedPlayers_ = ResolvePlayerCount(logicalPoint);
    nativeDragActive_ = false;
    const auto chosen = std::clamp(selectedPlayers_, 1, maximumPlayers_);
    sliderOpen_ = false;
    if (apply_ == nullptr || !apply_(chosen)) {
        if (context_ != nullptr) {
            context_->LogWarn(
                "Players HUD slider: the selected player count could not be applied.");
        }
    }
    nativePanel_.Close();
}

auto PlayerHudSlider::LogicalPoint(
        POINT screenPoint,
        POINT& output) const noexcept -> bool {
    if (hostWindow_ == nullptr || !IsWindow(hostWindow_)) return false;
    if (!ScreenToClient(hostWindow_, &screenPoint)) return false;
    RECT client{};
    if (!GetClientRect(hostWindow_, &client)) return false;
    const int height = client.bottom - client.top;
    if (height <= 0) return false;
    const double scale = static_cast<double>(height) / VirtualLayoutHeight;
    output.x = static_cast<LONG>(std::lround(screenPoint.x / scale));
    output.y = static_cast<LONG>(std::lround(screenPoint.y / scale));
    return true;
}

auto PlayerHudSlider::IsSliderPoint(POINT point) const noexcept -> bool {
    if (RectContains(point, config_.track.x, config_.track.y,
            config_.track.width, config_.track.height)) {
        return true;
    }
    const auto selected = std::clamp(selectedPlayers_, 1, maximumPlayers_);
    const auto markerX = static_cast<std::int32_t>(std::lround(MarkerLeft(selected)));
    if (RectContains(point, markerX, config_.marker.y,
            config_.marker.width, config_.marker.height)) {
        return true;
    }
    return false;
}

auto PlayerHudSlider::MarkerLeft(std::int32_t players) const noexcept -> double {
    const double startCenter = config_.marker.x + config_.marker.width / 2.0;
    const double endCenter = config_.track.x + config_.track.width
        - config_.marker.width / 2.0;
    if (maximumPlayers_ <= 1 || endCenter <= startCenter) {
        return config_.marker.x;
    }
    const double fraction = static_cast<double>(players - 1)
        / static_cast<double>(maximumPlayers_ - 1);
    return startCenter + fraction * (endCenter - startCenter)
        - config_.marker.width / 2.0;
}

auto PlayerHudSlider::MarkerCenter(std::int32_t players) const noexcept -> double {
    return MarkerLeft(players) + config_.marker.width / 2.0;
}

auto PlayerHudSlider::ResolvePlayerCount(POINT point) const noexcept
        -> std::int32_t {
    if (maximumPlayers_ <= 1) return 1;
    const double start = MarkerCenter(1);
    const double end = MarkerCenter(maximumPlayers_);
    if (end <= start) return 1;
    const double fraction = std::clamp(
        (static_cast<double>(point.x) - start) / (end - start), 0.0, 1.0);
    return std::clamp(static_cast<std::int32_t>(std::lround(
        1.0 + fraction * (maximumPlayers_ - 1))), 1, maximumPlayers_);
}

LRESULT CALLBACK PlayerHudSlider::WindowProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) noexcept {
    auto* self = reinterpret_cast<PlayerHudSlider*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(window, message, wParam, lParam);

    if (message == WM_TIMER && wParam == RefreshTimerId) {
        const HWND foreground = GetForegroundWindow();
        const bool hostIsForeground = foreground == self->hostWindow_
            || (foreground != nullptr && self->hostWindow_ != nullptr
                && IsChild(self->hostWindow_, foreground));
        const bool shouldShow = self->active_.load(std::memory_order_acquire)
            && self->sessionReady_ && hostIsForeground
            && (self->canApply_ == nullptr || self->canApply_());
        if (shouldShow != self->nativePanel_.IsHudButtonOpen()) {
            if (shouldShow) self->nativePanel_.ShowHudButton();
            else self->nativePanel_.HideHudButton();
        }
        self->sliderOpen_ = self->nativePanel_.IsOpen();
        if (!self->sliderOpen_) self->nativeDragActive_ = false;
        return 0;
    }
    if (message == WM_TIMER && wParam == NativeDragTimerId) {
        self->PollNativeSliderDrag();
        return 0;
    }
    if (message == DetachMessage) {
        (void)KillTimer(window, RefreshTimerId);
        (void)KillTimer(window, NativeDragTimerId);
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (self->previousWindowProc_ != nullptr) {
            (void)SetWindowLongPtrW(window, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(self->previousWindowProc_));
        }
        self->timerWindow_ = nullptr;
        self->previousWindowProc_ = nullptr;
        (void)PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        const auto previous = self->previousWindowProc_;
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->timerWindow_ = nullptr;
        self->previousWindowProc_ = nullptr;
        return previous != nullptr
            ? CallWindowProcW(previous, window, message, wParam, lParam)
            : DefWindowProcW(window, message, wParam, lParam);
    }

    return self->previousWindowProc_ != nullptr
        ? CallWindowProcW(self->previousWindowProc_, window,
            message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

} // namespace players_hud_slider
