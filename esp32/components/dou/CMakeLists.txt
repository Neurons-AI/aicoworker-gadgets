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

# Dou: the voice companion UI that runs beside Home Link on boards with a
# display (CONFIG_DOU_BOARD_*). Link-only builds register an empty component,
# except the Voice PE, which borrows the Dou voice session.
#
# Requirements are resolved before Kconfig is known, so everything any board
# needs is listed unconditionally. Managed components (LVGL, codecs, BSPs) come
# from idf_component.yml, where rules keep them out of Link-only builds.
set(srcs)
set(embed)
if(CONFIG_DOU_ENABLED)
    list(APPEND srcs
        "dou_app.c"
        "dou_board.c"
        "dou_console.c"
        "dou_state.c"
        "dou_options.c"
        "dou_battery.c"
        "dou_audio.c"
        "dou_pmu.c"
        "dou_input.c"
        "dou_voice.c"
        "dou_adpcm.c"
        "dou_settings.c"
        "dou_text.c"
        "dou_lang.c"
        "dou_link.c"
        "dou_ble.c")
    if(CONFIG_DOU_UI_THEME)
        list(APPEND srcs "dou_theme.c" "dou_theme_policy.c" "dou_theme_lvgl.c")
    endif()
    if(CONFIG_DOU_WAKEWORD_ENABLE)
        # Wake word tap and task; without it, dou_wakeword_glue.h's inline stubs.
        list(APPEND srcs "dou_wakeword_glue.c")
    endif()
    if(CONFIG_DOU_AVATAR_SYNC)
        list(APPEND srcs "dou_avatar_render.c")
        if(CONFIG_DOU_UI_THEME)
            list(APPEND srcs "dou_avatar_background.c" "dou_avatar_theme.c")
        endif()
    endif()
    if(CONFIG_DOU_MOTION_GESTURES)
        list(APPEND srcs "dou_motion.c" "dou_motion_core.c" "boards/qmi8658_motion.c")
    endif()
    if(CONFIG_DOU_BOARD_SENSECAP_WATCHER OR CONFIG_DOU_LATEST_REPLY)
        list(APPEND srcs "dou_review.c")
    endif()
    if(CONFIG_DOU_LATEST_REPLY)
        list(APPEND srcs "dou_latest_reply.c")
    endif()
    if(CONFIG_DOU_UI_FREEINK)
        # E-paper: FreeInk draws the face, Settings and the keyboard.
        list(APPEND srcs "eink/dou_eink_ui.cpp" "eink/dou_eink_settings.cpp" "eink/dou_eink_fonts.cpp")
        # The LVGL UI's fonts, as the TTFs LVGL builds them from (eink/fonts/).
        list(APPEND embed "eink/fonts/montserrat.ttf" "eink/fonts/symbols.ttf" "eink/fonts/unscii.ttf")
    else()
        list(APPEND srcs "dou_ui.c" "dou_options_ui.c" "dou_menu.c" "dou_settings_ui.c" "dou_keypad.c" "dou_lv_mem.c")
    endif()
    if(CONFIG_DOU_HATCH)
        if(CONFIG_DOU_AICW_BRIDGE)
            list(APPEND srcs "dou_chat.c" "dou_account_api.c" "aicw_chat_session.c" "aicw_tweetnacl.c" "dou_options_json.c")
        else()
            list(APPEND srcs "dou_chat.c" "dou_account_api.c" "dou_chat_session.cpp" "dou_options_json.c")
        endif()
        list(APPEND embed "test_reply.mp3")   # dou_hatch_mp3_selftest
        if(CONFIG_DOU_RESEND_NOTES)
            list(APPEND srcs "dou_resend.c")   # when a note that didn't send goes again
        endif()
        if(CONFIG_DOU_PHOTO_NOTES)
            list(APPEND srcs "dou_photo_note.c" "dou_chat_image.c")   # a photo with each voice note
        endif()
        if(CONFIG_DOU_REALTIME_VOICE)
            list(APPEND srcs "dou_voice_call.cpp" "dou_voice_call_core.c")
            if(CONFIG_DOU_VOICE_SOFTWARE_AEC)
                list(APPEND srcs "dou_aec.c")
            endif()
        endif()
    else()
        list(APPEND srcs "dou_chat_link.c")   # text replies over Link's session
    endif()
    list(APPEND srcs "dou_chat_text.c")
    if(CONFIG_DOU_CJK_FONT_NOTO)
        list(APPEND srcs "fonts/dou_font_cjk_noto_16.c")   # captions' CJK fallback
    elseif(CONFIG_DOU_CJK_FONT)
        list(APPEND srcs "fonts/dou_font_cjk_16.c")
    endif()
    if(CONFIG_DOU_LATIN_FONT)
        list(APPEND srcs "fonts/dou_font_latin_16.c")   # captions with accented letters
    endif()
    if(CONFIG_DOU_EMOJI)
        list(APPEND srcs "fonts/dou_font_emoji_16.c")   # captions' emoji
    endif()
    if(CONFIG_DOU_UI_LANG_VI)
        # Montserrat's Vietnamese letters, for the menu and the pairing card
        list(APPEND srcs "fonts/dou_font_vi_12.c" "fonts/dou_font_vi_14.c"
                         "fonts/dou_font_vi_20.c" "fonts/dou_font_vi_28.c")
    endif()
    if(CONFIG_DOU_UI_LANG_PT_BR)
        # Montserrat's Portuguese letters: menu, pairing card, settings pages
        list(APPEND srcs "fonts/dou_font_pt_12.c" "fonts/dou_font_pt_14.c" "fonts/dou_font_pt_16.c"
                         "fonts/dou_font_pt_20.c" "fonts/dou_font_pt_28.c")
    endif()
    # Your own character (tools/dou/AVATAR_RECIPE.md) stands in for the default
    # avatar when avatar/dou_pixel.c exists here; this avatar/ is gitignored. The
    # default is in avatar/ at the top of the project.
    file(GLOB custom_avatar CONFIGURE_DEPENDS "${COMPONENT_DIR}/avatar/dou_pixel.c")
    if(custom_avatar)
        message(STATUS "Custom avatar: components/dou/avatar/dou_pixel.c")
        list(APPEND srcs "avatar/dou_pixel.c")
    else()
        list(APPEND srcs "../../avatar/dou_pixel.c")
    endif()
    if(CONFIG_DOU_BOARD_WAVESHARE_S3_175C OR CONFIG_DOU_BOARD_WAVESHARE_S3_175)
        # One driver: the 1.75 differs only in pins (its BSP's) and its PWR key.
        list(APPEND srcs "boards/board_waveshare_s3_175c.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_OSTB_ECHOEAR_2ST)
        list(APPEND srcs "boards/board_ostb_echoear_2st.c")
    elseif(CONFIG_DOU_BOARD_RETERMINAL_STICKY)
        list(APPEND srcs "boards/board_reterminal_sticky.c")
        list(APPEND srcs "boards/board_reterminal_sticky_eink.cpp")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_216)
        list(APPEND srcs "boards/board_waveshare_s3_216.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_206)
        list(APPEND srcs "boards/board_waveshare_s3_206.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_185B)
        list(APPEND srcs "boards/board_waveshare_s3_185b.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_185C)
        list(APPEND srcs "boards/board_waveshare_s3_185c.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_143C)
        list(APPEND srcs "boards/board_waveshare_s3_143c.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_143)
        list(APPEND srcs "boards/board_waveshare_s3_143.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_146)
        list(APPEND srcs "boards/board_waveshare_s3_146.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_LILYGO_S3_AMOLED_175)
        list(APPEND srcs "boards/board_lilygo_s3_amoled_175.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_ESP_MOSAICO)
        list(APPEND srcs "boards/board_esp_mosaico.c" "boards/dou_lcd_bands.c")
        if(CONFIG_DOU_MOSAICO_CAMERA)
            list(APPEND srcs "boards/mosaico_camera.c")
        endif()
    elseif(CONFIG_DOU_BOARD_DFROBOT_UNIHIKER_K10)
        list(APPEND srcs "boards/board_dfrobot_unihiker_k10.c")
    elseif(CONFIG_DOU_BOARD_AIPI)
        list(APPEND srcs "boards/board_aipi.c")
    elseif(CONFIG_DOU_BOARD_ATK_DNESP32S3_BOX)
        list(APPEND srcs "boards/board_atk_dnesp32s3_box.c")
    elseif(CONFIG_DOU_BOARD_ATK_DNESP32S3_BOX2)
        list(APPEND srcs "boards/board_atk_dnesp32s3_box2.c")
    elseif(CONFIG_DOU_BOARD_ESPRESSIF_BOX_3)
        list(APPEND srcs "boards/board_espressif_box_3.c" "boards/box_3_microphone.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_C6_18)
        list(APPEND srcs "boards/board_waveshare_c6_18.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_18)
        list(APPEND srcs "boards/board_waveshare_s3_18.c" "boards/dou_lcd_bands.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_C6_206)
        list(APPEND srcs "boards/board_waveshare_c6_206.c")
    elseif(CONFIG_DOU_BOARD_SENSECAP_WATCHER)
        list(APPEND srcs "boards/board_sensecap_watcher.c" "boards/dou_lcd_bands.c")
        if(CONFIG_DOU_WATCHER_CAMERA)
            list(APPEND srcs "boards/watcher_camera.c")
        endif()
    elseif(CONFIG_DOU_BOARD_M5STACK_CARDPUTER_ADV)
        list(APPEND srcs "boards/board_m5stack_cardputer_adv.c")
    elseif(CONFIG_DOU_BOARD_M5STACK_ATOMS3)
        list(APPEND srcs "boards/board_m5stack_atoms3.c")
    elseif(CONFIG_DOU_BOARD_M5STACK_STICKS3)
        list(APPEND srcs "boards/board_m5stack_sticks3.c")
    elseif(CONFIG_DOU_BOARD_CROWPANEL_ADVANCE_43)
        list(APPEND srcs "boards/board_crowpanel_advance_43.c")
    elseif(CONFIG_DOU_BOARD_M5STACK_STICKC_PLUS2)
        list(APPEND srcs "boards/board_m5stack_stickc_plus2.c")
    elseif(CONFIG_DOU_BOARD_M5STACK_STOPWATCH_FAMILY)
        list(APPEND srcs "boards/board_m5stack_stopwatch.c" "boards/dou_lcd_bands.c")
        if(CONFIG_CAPABILITIES_REGISTRY)
            # BMI270 registry/lifecycle adapter; vendor driver is a gated managed dependency.
            list(APPEND srcs "boards/bmi270_device.c" "boards/imu_events.c")
        endif()
    elseif(CONFIG_DOU_BOARD_SEEED_XIAO_S3_147)
        list(APPEND srcs "boards/board_seeed_xiao_s3_147.c")
    elseif(CONFIG_DOU_BOARD_M5STACK_CORES3 OR CONFIG_DOU_BOARD_M5STACK_CORES3LITE)
        list(APPEND srcs "boards/board_m5stack_cores3.c")
    elseif(CONFIG_DOU_BOARD_M5STACK_CORE2)
        list(APPEND srcs "boards/board_m5stack_core2.c")
    elseif(CONFIG_DOU_BOARD_FNK0104B)
        list(APPEND srcs "boards/board_fnk0104b.c")
    elseif(CONFIG_DOU_BOARD_GUITION_JC3248W535)
        list(APPEND srcs "boards/board_guition_jc3248w535.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_LCD7)
        list(APPEND srcs "boards/board_waveshare_s3_lcd7.c")
    elseif(CONFIG_DOU_BOARD_SPOTPEAR_ESP32_AI_128)
        list(APPEND srcs "boards/board_spotpear_esp32_ai_128.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_LCD128)
        list(APPEND srcs "boards/board_waveshare_s3_lcd128.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_LCD349)
        list(APPEND srcs "boards/board_waveshare_s3_lcd349.c")
    elseif(CONFIG_DOU_BOARD_VN_S3_183)
        list(APPEND srcs "boards/board_vn_s3_183.c")
    elseif(CONFIG_DOU_BOARD_AI_PASSPORT)
        list(APPEND srcs "boards/board_ai_passport.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_P4_LCD35)
        list(APPEND srcs "boards/board_waveshare_p4_lcd35.c")
        if(CONFIG_DOU_P4_CSI_CAMERA)
            list(APPEND srcs "boards/p4_csi_camera.c")
        endif()
    elseif(CONFIG_DOU_BOARD_DEOTALAND_ROROLEE)
        list(APPEND srcs "boards/board_deotaland_rorolee.c")
    elseif(CONFIG_DOU_BOARD_VIEWE_SMARTRING_PLUS)
        list(APPEND srcs "boards/board_viewe_smartring_plus.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_LCD19)
        list(APPEND srcs "boards/board_waveshare_s3_lcd19.c")
    elseif(CONFIG_DOU_BOARD_WAVESHARE_S3_LCD_35)
        list(APPEND srcs "boards/board_waveshare_s3_lcd_35.c")
    endif()
    if(CONFIG_DOU_VIDEO OR CONFIG_DOU_AVATAR_ANIMATIONS)
        list(APPEND srcs "dou_vdec.c")
    endif()
    if(CONFIG_DOU_AVATAR_ANIMATIONS)
        list(APPEND srcs "dou_avatar_motion.c" "dou_avatar_pack.c")
    endif()
    if(CONFIG_DOU_VIDEO)
        list(APPEND srcs "dou_yuv.c")
        # Phone camera: decode and draw (dou_video), and the transports that feed it.
        list(APPEND srcs "dou_video.c")
        if(CONFIG_IDF_TARGET_ESP32S3)
            list(APPEND srcs "dou_yuv_pie.S")   # the S3's SIMD colour conversion
        elseif(CONFIG_IDF_TARGET_ESP32S31)
            list(APPEND srcs "dou_yuv_pie_s31.S")   # the same on the S31's SIMD (checked at first use)
        endif()
        if(CONFIG_DOU_VIDEO_WIFI)
            list(APPEND srcs "dou_video_wifi.c" "dou_video_wire.c")
        endif()
    endif()
    if(CONFIG_CAPABILITIES_REGISTRY)
        list(APPEND srcs "board_capabilities.c")    # the battery as a device for apps (components/capabilities_registry)
    endif()
    if(CONFIG_DOU_APP_VIEW)
        list(APPEND srcs "dou_app_view.c")   # the screen an app draws on (components/dou_apps)
    endif()
    # Shared core, independent of board selection. Keep it after the BSP so
    # Watcher's independently retained recovery markers keep their link order.
    if(CONFIG_DOU_POWER_TEST)
        list(APPEND srcs "dou_power_test.c")
        if(CONFIG_DOU_BOARD_RETERMINAL_STICKY)
            list(APPEND srcs "boards/board_reterminal_sticky_power_test.c" "boards/sticky_power_test_display.cpp")
        endif()
        if(CONFIG_DOU_BOARD_WAVESHARE_S3_175C OR CONFIG_DOU_BOARD_WAVESHARE_S3_216 OR CONFIG_DOU_BOARD_AIPI OR CONFIG_DOU_BOARD_ESP_MOSAICO)
            list(APPEND srcs "boards/board_power_test_generic.c")
        endif()
    endif()
elseif(CONFIG_HOMEHUB_VOICE)
    # The Voice PE board uses just the Dou voice session; the project's main/
    # voice_dou_chat.c stands in for the Dou settings, Wi-Fi and Link hooks.
    list(APPEND srcs "dou_chat.c" "dou_account_api.c" "dou_chat_session.cpp" "dou_chat_text.c" "dou_text.c")
    # The shared session handles option events even without a touchscreen.
    list(APPEND srcs "dou_options.c" "dou_options_json.c")
    list(APPEND embed "test_reply.mp3")
    if(CONFIG_DOU_REALTIME_VOICE)
        list(APPEND srcs "dou_voice_call.cpp" "dou_voice_call_core.c")
    endif()
endif()

set(dou_priv_requires
    esp_timer esp_driver_usb_serial_jtag esp_psram nvs_flash esp_pm esp_event esp_netif
    bt esp-tls mbedtls esp_app_format esp_http_client network_proxy
    noise_core minimp3 dou_vstream tinyh264 libvpx_vp8 dou_mirror capabilities_registry
    esp_lcd esp_adc esp_driver_i2s esp_driver_ledc esp_driver_spi esp_driver_uart esp_wifi
    esp_driver_pcnt efuse
    # The Sticky's microSD (CONFIG_DOU_STICKY_SD). Requirements are resolved
    # before Kconfig values exist, so these can't depend on it; only the code does.
    fatfs esp_driver_sdspi sdmmc)
list(APPEND dou_priv_requires camera)
# The P4 camera's JPEG encoder; Kconfig is not loaded yet when requirements expand.
if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND dou_priv_requires esp_driver_jpeg)
endif()
# The Mosaico camera's JPEG encoder and PPA; Kconfig is not loaded yet when
# requirements expand.
if(IDF_TARGET STREQUAL "esp32s31")
    list(APPEND dou_priv_requires esp_driver_jpeg esp_driver_ppa)
endif()
list(APPEND dou_priv_requires dou_wakeword)   # registers empty unless DOU_WAKEWORD_ENABLE

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    EMBED_FILES ${embed}
    LDFRAGMENTS "linker.lf"   # CONFIG_DOU_STATIC_BUFFERS_IN_PSRAM
    WHOLE_ARCHIVE   # dou_lv_mem.c provides symbols only LVGL references
    REQUIRES esp_driver_gpio esp_driver_i2c
    PRIV_REQUIRES ${dou_priv_requires}
)

if(CONFIG_DOU_UI_FREEINK)
    # FreeInk's board profile, for the panel and its pins. PUBLIC: FreeInk's
    # headers read it too, in every file that includes them.
    idf_component_get_property(freeink_lib freeink-sdk COMPONENT_LIB)
    # One framebuffer, the e-ink UI's DisplayTarget's: the panel's RAM keeps
    # the previous frame for fast refreshes (as FreeInk's samples build it).
    target_compile_definitions(${freeink_lib} PUBLIC "FREEINK_DEVICE_${CONFIG_DOU_FREEINK_DEVICE}=1"
                               "EINK_DISPLAY_SINGLE_BUFFER_MODE=1")
    # Dou's CJK font is LVGL's format: the e-ink UI reads it with LVGL's font
    # types only (eink/lvgl_font/lvgl.h), not LVGL.
    set_source_files_properties("fonts/dou_font_cjk_16.c" PROPERTIES
        COMPILE_OPTIONS "-I${COMPONENT_DIR}/eink/lvgl_font")
elseif(CONFIG_DOU_ENABLED)
    # LVGL spins forever on a failed assert unless LV_ASSERT_HANDLER is set,
    # which its Kconfig can't do. Abort, so the panic handler prints a backtrace
    # and reboots. PUBLIC reaches LVGL and every file that includes lvgl.h.
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    target_compile_definitions(${lvgl_lib} PUBLIC
        "LV_ASSERT_HANDLER_INCLUDE=<stdlib.h>"
        "LV_ASSERT_HANDLER=abort()\;")
endif()

if(CONFIG_DOU_AICW_BRIDGE)
    set_source_files_properties(aicw_tweetnacl.c PROPERTIES COMPILE_OPTIONS "-Wno-unterminated-string-initialization;-Wno-sign-compare;-Wno-unused-variable")
endif()
