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

# Optional appearance integration is tested when the independent theme is present.
if(EXISTS "${DOU_COMPONENT_DIR}/dou_theme.c")
    add_executable(dou_avatar_theme_ui_test tests/avatar_theme_ui_harness.c ${caption_sources}
        "${DOU_COMPONENT_DIR}/dou_avatar_render.c" "${DOU_COMPONENT_DIR}/dou_avatar_background.c" "${DOU_COMPONENT_DIR}/dou_avatar_theme.c"
        "${DOU_COMPONENT_DIR}/dou_theme.c" "${DOU_COMPONENT_DIR}/dou_theme_policy.c" "${DOU_COMPONENT_DIR}/dou_theme_lvgl.c")
    target_include_directories(dou_avatar_theme_ui_test PRIVATE ${caption_includes})
    target_link_libraries(dou_avatar_theme_ui_test PRIVATE ${caption_libraries})
    target_compile_options(dou_avatar_theme_ui_test PRIVATE ${caption_compile_options})
    target_compile_definitions(dou_avatar_theme_ui_test PRIVATE CONFIG_DOU_AVATAR_SYNC=1 CONFIG_DOU_AVATAR_ANIMATIONS=1 CONFIG_DOU_UI_THEME=1)
    if(DOU_SIM_SANITIZERS)
        target_link_options(dou_avatar_theme_ui_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    add_test(NAME dou_avatar_theme_ui COMMAND dou_avatar_theme_ui_test)
    set_tests_properties(dou_avatar_theme_ui PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1" TIMEOUT 30)
    if(TARGET dou_settings_theme_test)
        get_target_property(avatar_theme_settings_sources dou_settings_theme_test SOURCES)
        add_executable(dou_settings_avatar_theme_test ${avatar_theme_settings_sources})
        foreach(prop INCLUDE_DIRECTORIES LINK_LIBRARIES COMPILE_OPTIONS LINK_OPTIONS COMPILE_DEFINITIONS)
            get_target_property(value dou_settings_theme_test ${prop})
            if(value)
                set_property(TARGET dou_settings_avatar_theme_test PROPERTY ${prop} "${value}")
            endif()
        endforeach()
        target_compile_definitions(dou_settings_avatar_theme_test PRIVATE CONFIG_DOU_AVATAR_SYNC=1)
        add_test(NAME dou_settings_avatar_theme_navigation_466 COMMAND dou_settings_avatar_theme_test 466 466 1 1.75 theme-navigation)
        add_test(NAME dou_settings_avatar_theme_navigation_240 COMMAND dou_settings_avatar_theme_test 240 240 1 1.28 theme-navigation)
        add_test(NAME dou_settings_avatar_theme_navigation_flat COMMAND dou_settings_avatar_theme_test 320 240 0 2.0 theme-navigation)
        set_tests_properties(dou_settings_avatar_theme_navigation_466 dou_settings_avatar_theme_navigation_240 dou_settings_avatar_theme_navigation_flat
            PROPERTIES ENVIRONMENT "${DOU_SIM_TEST_ENVIRONMENT}" TIMEOUT 30)
    endif()
endif()
