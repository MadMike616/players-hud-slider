cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PlayersHudSlider VERSION 1.1.17 LANGUAGES CXX RC)
    set(PLAYERS_HUD_PLUGIN_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PluginSDK")
    add_library(D2RLPlugin INTERFACE)
    add_library(D2RLPlugin::D2RLPlugin ALIAS D2RLPlugin)
    target_include_directories(D2RLPlugin INTERFACE "${PLAYERS_HUD_PLUGIN_SDK_DIR}/include")
    target_compile_features(D2RLPlugin INTERFACE cxx_std_20)
    include("${PLAYERS_HUD_PLUGIN_SDK_DIR}/cmake/D2RLPluginEmbedConfig.cmake")
else()
    enable_language(RC)
endif()
include(FetchContent)
if(NOT TARGET tomlplusplus::tomlplusplus)
    set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(tomlplusplus)
endif()
if(WIN32)
    find_program(PLAYERS_HUD_SLIDER_POWERSHELL NAMES pwsh.exe powershell.exe)
    if(NOT PLAYERS_HUD_SLIDER_POWERSHELL)
        message(FATAL_ERROR "PowerShell is required to build the native D2R slider sprites")
    endif()
    execute_process(
        COMMAND "${PLAYERS_HUD_SLIDER_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-native-panel-sprites.ps1"
            -PluginRoot "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE PLAYERS_HUD_SPRITE_RESULT
        OUTPUT_VARIABLE PLAYERS_HUD_SPRITE_OUTPUT
        ERROR_VARIABLE PLAYERS_HUD_SPRITE_ERROR)
    if(NOT PLAYERS_HUD_SPRITE_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not generate native panel sprites: ${PLAYERS_HUD_SPRITE_ERROR}")
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-native-panel-sprites.ps1"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/artwork/hud-player-slider-button-states.png")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/players-hud-slider.toml"
    PLAYERS_HUD_SLIDER_DEFAULT_TOML)
configure_file(src/default_config.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/default_config.hpp"
    @ONLY NEWLINE_STYLE LF)
add_library(players_hud_slider SHARED
    src/plugin.cpp src/player_hud_slider.cpp src/native_slider_panel.cpp src/plugin.rc)
target_compile_features(players_hud_slider PRIVATE cxx_std_20)
target_compile_definitions(players_hud_slider PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_include_directories(players_hud_slider PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(players_hud_slider PRIVATE
    D2RLPlugin::D2RLPlugin tomlplusplus::tomlplusplus
    user32 gdi32 gdiplus ole32)
set_property(SOURCE src/plugin.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/artwork/hud-player-slider-button.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/artwork/hud-player-slider-button-states.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/artwork/hud-player-slider-track.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/artwork/hud-player-slider-marker.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/artwork/hud-player-slider-tooltip.png")
set_property(SOURCE src/plugin.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/native-track.sprite"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/native-marker.sprite"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/native-tooltip.sprite"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/native/native-button.sprite")
set_target_properties(players_hud_slider PROPERTIES
    OUTPUT_NAME "d2rl-players-hud-slider" PREFIX "")
d2rlplugin_embed_config(TARGET players_hud_slider
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/players-hud-slider.toml")
if(MSVC)
    target_compile_options(players_hud_slider PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/permissive-;/EHsc>)
    target_link_options(players_hud_slider PRIVATE /INCREMENTAL:NO /Brepro)
endif()
