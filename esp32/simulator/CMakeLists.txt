# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.24)

project(dou_ui_simulator VERSION 0.1.0 LANGUAGES C)

if(WIN32 OR NOT UNIX)
    message(FATAL_ERROR "The UI simulator currently supports Linux and macOS")
endif()
if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "The UI simulator requires GCC or Clang")
endif()

include(FetchContent)
find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DOU_SIM_FETCH_DEPS
       "Fetch pinned LVGL and SDL2 sources when suitable system packages are unavailable"
       ON)
option(DOU_SIM_SANITIZERS "Build the simulator with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(DOU_SIM_WARNINGS_AS_ERRORS "Treat simulator warnings as errors" OFF)
option(DOU_SIM_EMOJI "Show emoji in captions, as CONFIG_DOU_EMOJI does" OFF)

set(DOU_SIM_LVGL_VERSION "9.5.0")
set(DOU_SIM_SDL_MIN_VERSION "2.0.18")
set(DOU_SIM_SDL_FETCH_VERSION "2.32.10")

find_package(PkgConfig QUIET)

# Prefer a system SDL2 so desktop packaging can control its display backends.
# SDL2's CMake package is not installed on every distribution, so pkg-config is
# also accepted before falling back to the pinned upstream archive.
find_package(SDL2 ${DOU_SIM_SDL_MIN_VERSION} CONFIG QUIET)
if(TARGET SDL2::SDL2)
    set(DOU_SIM_SDL_TARGET SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
    set(DOU_SIM_SDL_TARGET SDL2::SDL2-static)
elseif(PkgConfig_FOUND)
    pkg_check_modules(DOU_SIM_SYSTEM_SDL QUIET IMPORTED_TARGET GLOBAL
                      "sdl2>=${DOU_SIM_SDL_MIN_VERSION}")
    if(DOU_SIM_SYSTEM_SDL_FOUND)
        set(DOU_SIM_SDL_TARGET PkgConfig::DOU_SIM_SYSTEM_SDL)
    endif()
endif()

if(NOT DOU_SIM_SDL_TARGET)
    if(NOT DOU_SIM_FETCH_DEPS)
        message(FATAL_ERROR
                "SDL2 >= ${DOU_SIM_SDL_MIN_VERSION} was not found and "
                "DOU_SIM_FETCH_DEPS is OFF")
    endif()

    # Build only the static SDL runtime. Tests and the SDL test helper library
    # add substantial compile time without being used by this executable.
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_WERROR OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
    set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
    set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL2
        URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-${DOU_SIM_SDL_FETCH_VERSION}.tar.gz"
        URL_HASH "SHA256=03f9d7c191a837525c9cda6406af2f2e48be02b5e7eb03d949cc9f1e9ca41c8b"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL2)
    set(DOU_SIM_SDL_TARGET SDL2::SDL2-static)
endif()

# The UI depends on LVGL 9.5 private APIs and on the fonts, SDL driver, and
# renderer settings in this directory's lv_conf.h. Use the pinned source by
# default so an exact-version system package with a different configuration
# cannot be selected accidentally. Turning dependency fetching off explicitly
# opts into a compatible system LVGL build.
if(NOT DOU_SIM_FETCH_DEPS)
    find_package(lvgl ${DOU_SIM_LVGL_VERSION} EXACT CONFIG QUIET)
    if(TARGET lvgl::lvgl)
        set(DOU_SIM_LVGL_TARGET lvgl::lvgl)
    elseif(TARGET lvgl)
        set(DOU_SIM_LVGL_TARGET lvgl)
    elseif(PkgConfig_FOUND)
        pkg_check_modules(DOU_SIM_SYSTEM_LVGL QUIET IMPORTED_TARGET GLOBAL
                          "lvgl=${DOU_SIM_LVGL_VERSION}")
        if(DOU_SIM_SYSTEM_LVGL_FOUND)
            foreach(_include_dir IN LISTS DOU_SIM_SYSTEM_LVGL_INCLUDE_DIRS)
                if(EXISTS "${_include_dir}/src/draw/lv_image_decoder_private.h" AND
                   EXISTS "${_include_dir}/src/misc/lv_area_private.h")
                    set(DOU_SIM_LVGL_TARGET PkgConfig::DOU_SIM_SYSTEM_LVGL)
                    break()
                endif()
            endforeach()
        endif()
    endif()
    if(NOT DOU_SIM_LVGL_TARGET)
        message(FATAL_ERROR
                "LVGL ${DOU_SIM_LVGL_VERSION} with the simulator configuration "
                "was not found and DOU_SIM_FETCH_DEPS is OFF")
    endif()
else()
    set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
    set(LV_BUILD_SET_CONFIG_OPTS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_PRIVATE_API ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        lvgl
        URL "https://github.com/lvgl/lvgl/archive/refs/tags/v${DOU_SIM_LVGL_VERSION}.tar.gz"
        URL_HASH "SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(lvgl)
    set(DOU_SIM_LVGL_TARGET lvgl::lvgl)

    # Both LVGL's SDL display driver and its SDL2 OS abstraction include SDL
    # headers while LVGL itself is compiled.
    target_link_libraries(lvgl PRIVATE ${DOU_SIM_SDL_TARGET})
    # usleep() was removed from POSIX.1-2008, so _POSIX_C_SOURCE=200809L
    # alone hides its declaration on glibc; _DEFAULT_SOURCE re-exposes it.
    # LVGL's pthread OS layer calls usleep(), and GCC 14+ turns the
    # resulting implicit declaration into an error.
    target_compile_definitions(lvgl PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
endif()

set(DOU_COMPONENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../components/dou")

# Match the firmware's custom avatar override, including additions/removals
# after configure. Keep exactly one renderer in the executable.
file(GLOB DOU_SIM_CUSTOM_AVATAR CONFIGURE_DEPENDS
     "${DOU_COMPONENT_DIR}/avatar/dou_pixel.c")
if(DOU_SIM_CUSTOM_AVATAR)
    set(DOU_SIM_AVATAR_SOURCE "${DOU_COMPONENT_DIR}/avatar/dou_pixel.c")
else()
    set(DOU_SIM_AVATAR_SOURCE "${DOU_COMPONENT_DIR}/../../avatar/dou_pixel.c")
endif()

add_executable(dou_simulator
    src/main.c
    src/sim_board.c
    src/sim_platform.c
    src/sim_services.c
    "${DOU_COMPONENT_DIR}/dou_ui.c"
    "${DOU_COMPONENT_DIR}/dou_options.c"
    "${DOU_COMPONENT_DIR}/dou_options_ui.c"
    "${DOU_COMPONENT_DIR}/dou_state.c"
    "${DOU_COMPONENT_DIR}/dou_text.c"
    "${DOU_COMPONENT_DIR}/dou_lang.c"
    "${DOU_SIM_AVATAR_SOURCE}"
)

target_include_directories(dou_simulator PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${DOU_COMPONENT_DIR}"
)

target_link_libraries(dou_simulator PRIVATE
    ${DOU_SIM_LVGL_TARGET}
    ${DOU_SIM_SDL_TARGET}
    Threads::Threads
)

target_link_libraries(dou_simulator PRIVATE m)

if(APPLE)
    # SDL's Cocoa display and screensaver backends use IOKit even when its
    # optional power subsystem is disabled. The static target does not carry
    # that framework in this configuration.
    find_library(DOU_SIM_IOKIT_FRAMEWORK IOKit REQUIRED)
    target_link_libraries(dou_simulator PRIVATE ${DOU_SIM_IOKIT_FRAMEWORK})
endif()

if(DOU_SIM_EMOJI)
    target_sources(dou_simulator PRIVATE "${DOU_COMPONENT_DIR}/fonts/dou_font_emoji_16.c")
    target_compile_definitions(dou_simulator PRIVATE CONFIG_DOU_EMOJI=1)
endif()

target_compile_options(dou_simulator PRIVATE -Wall -Wextra -Wpedantic)
if(DOU_SIM_WARNINGS_AS_ERRORS)
    target_compile_options(dou_simulator PRIVATE -Werror)
endif()

if(DOU_SIM_SANITIZERS)
    target_compile_options(dou_simulator PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(dou_simulator PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
endif()

include(CTest)
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_simulator.py")
    # Reuse the simulator adapters, but include the production UI directly so
    # the regression can check the rendered label's font and geometry.
    get_target_property(caption_sources dou_simulator SOURCES)
    list(REMOVE_ITEM caption_sources src/main.c "${DOU_COMPONENT_DIR}/dou_ui.c")
    add_executable(dou_caption_layout_test tests/caption_layout_harness.c ${caption_sources})
    get_target_property(caption_includes dou_simulator INCLUDE_DIRECTORIES)
    get_target_property(caption_libraries dou_simulator LINK_LIBRARIES)
    target_include_directories(dou_caption_layout_test PRIVATE ${caption_includes})
    target_link_libraries(dou_caption_layout_test PRIVATE ${caption_libraries})
    get_target_property(caption_compile_options dou_simulator COMPILE_OPTIONS)
    target_compile_options(dou_caption_layout_test PRIVATE ${caption_compile_options})
    if(DOU_SIM_SANITIZERS)
        target_link_options(dou_caption_layout_test PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME dou_caption_layout COMMAND dou_caption_layout_test)
    add_executable(dou_theme_ui_test tests/theme_ui_harness.c ${caption_sources}
        "${DOU_COMPONENT_DIR}/dou_theme.c" "${DOU_COMPONENT_DIR}/dou_theme_policy.c" "${DOU_COMPONENT_DIR}/dou_theme_lvgl.c")
    target_include_directories(dou_theme_ui_test PRIVATE ${caption_includes})
    target_link_libraries(dou_theme_ui_test PRIVATE ${caption_libraries})
    target_compile_options(dou_theme_ui_test PRIVATE ${caption_compile_options})
    target_compile_definitions(dou_theme_ui_test PRIVATE CONFIG_DOU_UI_THEME=1)
    if(DOU_SIM_SANITIZERS)
        target_link_options(dou_theme_ui_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME dou_theme_ui COMMAND dou_theme_ui_test)
    set_tests_properties(dou_theme_ui PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1" TIMEOUT 30)
    add_executable(dou_avatar_ui_test tests/avatar_ui_harness.c ${caption_sources}
        "${DOU_COMPONENT_DIR}/dou_avatar_render.c")
    target_include_directories(dou_avatar_ui_test PRIVATE ${caption_includes})
    target_link_libraries(dou_avatar_ui_test PRIVATE ${caption_libraries})
    target_compile_options(dou_avatar_ui_test PRIVATE ${caption_compile_options})
    target_compile_definitions(dou_avatar_ui_test PRIVATE CONFIG_DOU_AVATAR_SYNC=1)
    if(DOU_SIM_SANITIZERS)
        target_link_options(dou_avatar_ui_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME dou_avatar_ui COMMAND dou_avatar_ui_test)
    set_tests_properties(dou_avatar_ui PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1" TIMEOUT 30)
    add_executable(dou_avatar_motion_ui_test tests/avatar_motion_ui_harness.c ${caption_sources}
        "${DOU_COMPONENT_DIR}/dou_avatar_render.c")
    target_include_directories(dou_avatar_motion_ui_test PRIVATE ${caption_includes})
    target_link_libraries(dou_avatar_motion_ui_test PRIVATE ${caption_libraries})
    target_compile_options(dou_avatar_motion_ui_test PRIVATE ${caption_compile_options})
    target_compile_definitions(dou_avatar_motion_ui_test PRIVATE CONFIG_DOU_AVATAR_SYNC=1 CONFIG_DOU_AVATAR_ANIMATIONS=1)
    if(DOU_SIM_SANITIZERS)
        target_link_options(dou_avatar_motion_ui_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME dou_avatar_motion_ui COMMAND dou_avatar_motion_ui_test)
    set_tests_properties(dou_avatar_motion_ui PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1" TIMEOUT 30)

    add_executable(dou_volume_swipe_test tests/volume_swipe_harness.c ${caption_sources})
    target_include_directories(dou_volume_swipe_test PRIVATE ${caption_includes})
    target_link_libraries(dou_volume_swipe_test PRIVATE ${caption_libraries})
    target_compile_options(dou_volume_swipe_test PRIVATE ${caption_compile_options})
    if(DOU_SIM_SANITIZERS)
        target_link_options(dou_volume_swipe_test PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME dou_volume_swipe COMMAND dou_volume_swipe_test)
    set_tests_properties(dou_volume_swipe PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
    add_executable(dou_options_ui_test tests/options_ui_harness.c ${caption_sources})
    target_include_directories(dou_options_ui_test PRIVATE ${caption_includes})
    target_link_libraries(dou_options_ui_test PRIVATE ${caption_libraries})
    target_compile_options(dou_options_ui_test PRIVATE ${caption_compile_options})
    if(DOU_SIM_SANITIZERS)
        target_link_options(dou_options_ui_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME dou_options_ui COMMAND dou_options_ui_test)
    add_test(NAME dou_options_round_466 COMMAND dou_options_ui_test 466 466 1)
    add_test(NAME dou_options_rectangle COMMAND dou_options_ui_test 320 480 0)
    set_tests_properties(dou_options_ui dou_options_round_466 dou_options_rectangle PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1" TIMEOUT 30)
    set_tests_properties(dou_caption_layout PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
    add_executable(dou_latest_reply_test tests/latest_reply_harness.c ${caption_sources}
        "${DOU_COMPONENT_DIR}/dou_review.c"
        "${DOU_COMPONENT_DIR}/dou_chat_text.c")
    target_include_directories(dou_latest_reply_test PRIVATE ${caption_includes})
    target_link_libraries(dou_latest_reply_test PRIVATE ${caption_libraries})
    target_compile_options(dou_latest_reply_test PRIVATE ${caption_compile_options})
    target_compile_definitions(dou_latest_reply_test PRIVATE CONFIG_DOU_LATEST_REPLY=1)
    if(DOU_SIM_SANITIZERS)
        target_link_options(dou_latest_reply_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME dou_latest_reply COMMAND dou_latest_reply_test)
    set_tests_properties(dou_latest_reply PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME dou_simulator_headless
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_simulator.py"
                --binary "$<TARGET_FILE:dou_simulator>"
    )
    set(DOU_SIM_TEST_ENVIRONMENT
        "SDL_VIDEODRIVER=dummy"
        "SDL_AUDIODRIVER=dummy"
    )
    if(DOU_SIM_SANITIZERS)
        # Apple Clang's ASan runtime rejects leak detection on macOS.
        if(APPLE)
            set(DOU_SIM_ASAN_OPTIONS "detect_leaks=0:halt_on_error=1")
        else()
            set(DOU_SIM_ASAN_OPTIONS "detect_leaks=1:halt_on_error=1")
        endif()
        list(APPEND DOU_SIM_TEST_ENVIRONMENT
            "ASAN_OPTIONS=${DOU_SIM_ASAN_OPTIONS}"
            "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
        )
    endif()
    set_tests_properties(dou_simulator_headless PROPERTIES
        ENVIRONMENT "${DOU_SIM_TEST_ENVIRONMENT}"
        TIMEOUT 60
    )

    add_executable(dou_settings_text_test
        tests/settings_text_harness.c
        "${DOU_COMPONENT_DIR}/dou_keypad.c"
        "${DOU_COMPONENT_DIR}/dou_lang.c"
        "${DOU_COMPONENT_DIR}/dou_text.c"
    )
    target_include_directories(dou_settings_text_test PRIVATE
        tests/settings_fakes compat ../tests/link_fakes "${DOU_COMPONENT_DIR}"
    )
    target_compile_definitions(dou_settings_text_test PRIVATE _POSIX_C_SOURCE=200809L)
    add_executable(dou_settings_theme_test
        tests/settings_text_harness.c "${DOU_COMPONENT_DIR}/dou_keypad.c"
        "${DOU_COMPONENT_DIR}/dou_lang.c" "${DOU_COMPONENT_DIR}/dou_text.c"
        "${DOU_COMPONENT_DIR}/dou_theme.c" "${DOU_COMPONENT_DIR}/dou_theme_policy.c" "${DOU_COMPONENT_DIR}/dou_theme_lvgl.c")
    target_include_directories(dou_settings_theme_test PRIVATE tests/settings_fakes compat ../tests/link_fakes "${DOU_COMPONENT_DIR}")
    target_compile_definitions(dou_settings_theme_test PRIVATE _POSIX_C_SOURCE=200809L CONFIG_DOU_UI_THEME=1)
    target_compile_options(dou_settings_theme_test PRIVATE -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections)
    target_link_libraries(dou_settings_theme_test PRIVATE ${DOU_SIM_LVGL_TARGET} ${DOU_SIM_SDL_TARGET} Threads::Threads m)
    if(APPLE)
        target_compile_definitions(dou_settings_theme_test PRIVATE _DARWIN_C_SOURCE)
        target_link_options(dou_settings_theme_test PRIVATE -Wl,-dead_strip)
        target_link_libraries(dou_settings_theme_test PRIVATE ${DOU_SIM_IOKIT_FRAMEWORK})
    else()
        target_link_options(dou_settings_theme_test PRIVATE -Wl,--gc-sections)
    endif()
    if(DOU_SIM_SANITIZERS)
        target_compile_options(dou_settings_theme_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(dou_settings_theme_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME dou_settings_theme_466 COMMAND dou_settings_theme_test 466 466 1 1.75 theme)
    add_test(NAME dou_settings_theme_240 COMMAND dou_settings_theme_test 240 240 1 1.28 theme)
    add_test(NAME dou_settings_theme_navigation COMMAND dou_settings_theme_test 466 466 1 1.75 theme-navigation)
    set_tests_properties(dou_settings_theme_navigation PROPERTIES ENVIRONMENT "${DOU_SIM_TEST_ENVIRONMENT}" TIMEOUT 30)
    set_tests_properties(dou_settings_theme_466 dou_settings_theme_240 PROPERTIES ENVIRONMENT "${DOU_SIM_TEST_ENVIRONMENT}" TIMEOUT 30)

    add_test(NAME dou_settings_volume_sync COMMAND dou_settings_text_test 466 466 1 1.75 volume)
    target_compile_options(dou_settings_text_test PRIVATE -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections)
    target_link_libraries(dou_settings_text_test PRIVATE ${DOU_SIM_LVGL_TARGET} ${DOU_SIM_SDL_TARGET} Threads::Threads m)
    if(APPLE)
        target_compile_definitions(dou_settings_text_test PRIVATE _DARWIN_C_SOURCE)
        target_link_options(dou_settings_text_test PRIVATE -Wl,-dead_strip)
        target_link_libraries(dou_settings_text_test PRIVATE ${DOU_SIM_IOKIT_FRAMEWORK})
    else()
        target_link_options(dou_settings_text_test PRIVATE -Wl,--gc-sections)
    endif()
    if(DOU_SIM_WARNINGS_AS_ERRORS)
        target_compile_options(dou_settings_text_test PRIVATE -Werror)
    endif()
    if(DOU_SIM_SANITIZERS)
        target_compile_options(dou_settings_text_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(dou_settings_text_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    get_target_property(avatar_settings_sources dou_settings_text_test SOURCES)
    add_executable(dou_settings_avatar_test ${avatar_settings_sources})
    foreach(prop INCLUDE_DIRECTORIES LINK_LIBRARIES COMPILE_OPTIONS LINK_OPTIONS COMPILE_DEFINITIONS)
        get_target_property(value dou_settings_text_test ${prop})
        if(value)
            set_property(TARGET dou_settings_avatar_test PROPERTY ${prop} "${value}")
        endif()
    endforeach()
    target_compile_definitions(dou_settings_avatar_test PRIVATE CONFIG_DOU_AVATAR_SYNC=1)
    add_test(NAME dou_settings_avatar_466 COMMAND dou_settings_avatar_test 466 466 1 1.75 avatar)
    add_test(NAME dou_settings_avatar_240 COMMAND dou_settings_avatar_test 240 240 1 1.28 avatar)
    set_tests_properties(dou_settings_avatar_466 dou_settings_avatar_240 PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")
    foreach(scenario other secure saved back)
        add_test(NAME dou_settings_xiao_${scenario}
                 COMMAND dou_settings_text_test 172 320 0 1.47 ${scenario})
    endforeach()
    add_test(NAME dou_settings_round_466 COMMAND dou_settings_text_test 466 466 1 1.75 geometry)
    add_test(NAME dou_settings_round_412 COMMAND dou_settings_text_test 412 412 1 1.45 geometry)
    add_test(NAME dou_settings_round_240 COMMAND dou_settings_text_test 240 240 1 1.28 geometry)
    add_test(NAME dou_settings_flat_keyboard COMMAND dou_settings_text_test 320 240 0 2.0 geometry)
    add_test(NAME dou_settings_flat_keyboard_password COMMAND dou_settings_text_test 320 240 0 2.0 password)
    add_test(NAME dou_settings_portrait_keyboard COMMAND dou_settings_text_test 240 320 0 2.8 geometry)
    add_test(NAME dou_settings_portrait_keyboard_password COMMAND dou_settings_text_test 240 320 0 2.8 password)
    add_test(NAME dou_settings_tall_keyboard COMMAND dou_settings_text_test 320 480 0 3.5 geometry)
    add_test(NAME dou_settings_epaper_keyboard COMMAND dou_settings_text_test 480 800 0 3.97 geometry)
    add_test(NAME dou_settings_wide_800 COMMAND dou_settings_text_test 800 480 0 4.3 geometry)
    add_test(NAME dou_settings_long_keypad COMMAND dou_settings_text_test 172 640 0 3.49 geometry)
    add_test(NAME dou_settings_xiao_switch COMMAND dou_settings_text_test 172 320 0 1.47 switch)
    add_test(NAME dou_settings_round_switch COMMAND dou_settings_text_test 466 466 1 1.75 switch)
    add_test(NAME dou_settings_round_240_switch COMMAND dou_settings_text_test 240 240 1 1.28 switch)
    add_test(NAME dou_settings_round_240_rows COMMAND dou_settings_text_test 240 240 1 1.28 rows)
    add_test(NAME dou_settings_xiao_rows COMMAND dou_settings_text_test 172 320 0 1.47 rows)
    add_test(NAME dou_settings_round_240_screen COMMAND dou_settings_text_test 240 240 1 1.28 screen)
    add_test(NAME dou_settings_round_240_mic COMMAND dou_settings_text_test 240 240 1 1.28 mic)
    add_test(NAME dou_settings_round_240_bench COMMAND dou_settings_text_test 240 240 1 1.28 bench)
    # The button menu's layout, in English and in Vietnamese (accented letters
    # in the 16 px caption font), on compact and full-size screens.
    foreach(lang en vi)
        add_executable(dou_menu_layout_${lang}
            tests/menu_layout_harness.c
            "${DOU_COMPONENT_DIR}/dou_lang.c"
            "${DOU_COMPONENT_DIR}/dou_text.c"
        )
        target_include_directories(dou_menu_layout_${lang} PRIVATE
            tests/settings_fakes compat ../tests/link_fakes "${DOU_COMPONENT_DIR}"
        )
        target_compile_definitions(dou_menu_layout_${lang} PRIVATE _POSIX_C_SOURCE=200809L)
        if(lang STREQUAL "vi")
            target_sources(dou_menu_layout_${lang} PRIVATE
                "${DOU_COMPONENT_DIR}/fonts/dou_font_latin_16.c"
                "${DOU_COMPONENT_DIR}/fonts/dou_font_vi_14.c"
                "${DOU_COMPONENT_DIR}/fonts/dou_font_vi_20.c"
                "${DOU_COMPONENT_DIR}/fonts/dou_font_vi_28.c"
            )
            target_compile_definitions(dou_menu_layout_${lang} PRIVATE
                CONFIG_DOU_LATIN_FONT=1 CONFIG_DOU_UI_LANG_VI=1)
        endif()
        target_compile_options(dou_menu_layout_${lang} PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(dou_menu_layout_${lang} PRIVATE ${DOU_SIM_LVGL_TARGET} ${DOU_SIM_SDL_TARGET} Threads::Threads m)
        if(APPLE)
            target_compile_definitions(dou_menu_layout_${lang} PRIVATE _DARWIN_C_SOURCE)
            target_link_libraries(dou_menu_layout_${lang} PRIVATE ${DOU_SIM_IOKIT_FRAMEWORK})
        endif()
        if(DOU_SIM_WARNINGS_AS_ERRORS)
            target_compile_options(dou_menu_layout_${lang} PRIVATE -Werror)
        endif()
        if(DOU_SIM_SANITIZERS)
            target_compile_options(dou_menu_layout_${lang} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(dou_menu_layout_${lang} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        endif()
        foreach(size 128x128 240x135 284x240 320x240 240x240)
            string(REPLACE "x" ";" wh ${size})
            add_test(NAME dou_settings_menu_${lang}_${size} COMMAND dou_menu_layout_${lang} ${wh})
        endforeach()
    endforeach()
    get_property(settings_tests DIRECTORY PROPERTY TESTS)
    list(FILTER settings_tests INCLUDE REGEX "^dou_settings_")
    set_tests_properties(${settings_tests} PROPERTIES ENVIRONMENT "${DOU_SIM_TEST_ENVIRONMENT}" TIMEOUT 30)
    include("${CMAKE_CURRENT_SOURCE_DIR}/avatar_theme_tests.cmake")

endif()

message(STATUS "Dou simulator LVGL target: ${DOU_SIM_LVGL_TARGET}")
message(STATUS "Dou simulator SDL2 target: ${DOU_SIM_SDL_TARGET}")
