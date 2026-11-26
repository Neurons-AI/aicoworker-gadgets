/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"
#if CONFIG_DOU_UI_THEME
#include "dou_theme_policy.h"
#endif
#if CONFIG_DOU_AVATAR_SYNC
#include "dou_avatar_render.h"
#endif

/*
 * User settings, persisted in NVS. Setters save immediately and then notify the
 * change listener (from the caller's task), which applies the setting to the
 * hardware. Brightness, auto-sleep and the speaker are polled where they're used,
 * as is the reply text size, which notifies nothing.
 */

#define DOU_SSID_MAX 32
#define DOU_PASS_MAX 64
#define DOU_HOST_MAX 63
#define DOU_VM_MAX 63
#define DOU_TOKEN_MAX 1023

#define DOU_MIC_GAIN_MAX 36      /* dB; ES7210 PGA, applied in 3 dB steps */

typedef enum {
    DOU_SETTING_VOLUME,
    DOU_SETTING_SPEAKER,
    DOU_SETTING_MIC_GAIN,
    DOU_SETTING_BRIGHTNESS,
    DOU_SETTING_SLEEP,
    DOU_SETTING_WIFI,          /* on/off or credentials */
    DOU_SETTING_BLE,
    DOU_SETTING_HATCH,
    DOU_SETTING_VOICE_NOTES,
#if CONFIG_DOU_AVATAR_SYNC
    DOU_SETTING_AVATAR,
#endif
#if CONFIG_DOU_WAKEWORD_ENABLE
    DOU_SETTING_WAKEWORD,      /* the wake word opt-in changed */
#endif
} dou_setting_t;

typedef void (*dou_setting_cb_t)(dou_setting_t what);

esp_err_t dou_settings_init(void);
void dou_settings_set_listener(dou_setting_cb_t cb);
#if CONFIG_DOU_AVATAR_SYNC
dou_avatar_source_t dou_settings_avatar_source(void);
/* Call from the backend's flash-safe worker, not a UI event handler. */
esp_err_t dou_settings_set_avatar_source(dou_avatar_source_t source);
#endif

#if CONFIG_DOU_UI_THEME
dou_theme_mode_t dou_settings_theme(void);
esp_err_t dou_settings_set_theme(dou_theme_mode_t mode);
#endif

int dou_settings_volume(void);         /* 0..100 */
bool dou_settings_speaker_on(void);    /* off: replies are shown, not played */
int dou_settings_mic_gain(void);       /* dB, 0..DOU_MIC_GAIN_MAX */
int dou_settings_brightness(void);     /* 10..100 */
int dou_settings_sleep_s(void);        /* 0 = never */
#if CONFIG_DOU_MOTION_GESTURES
bool dou_settings_lift_wake(void);
bool dou_settings_face_down_quiet(void);
void dou_settings_set_lift_wake(bool on);
void dou_settings_set_face_down_quiet(bool on);
#endif
/* The size of reply text on screen, from Extra small to Extra large. Until
 * it's chosen, the screen's own default (dou_settings_set_text_size_default). */
enum { DOU_TEXT_XSMALL, DOU_TEXT_SMALL, DOU_TEXT_MEDIUM, DOU_TEXT_LARGE, DOU_TEXT_XLARGE, DOU_TEXT_SIZES };
int dou_settings_text_size(void);
/* The UI's size for a person who hasn't chosen one: what the screen showed
 * before the setting existed. Not saved. */
void dou_settings_set_text_size_default(int size);
bool dou_settings_wifi_on(void);
bool dou_settings_ble_on(void);
/* StopWatch: false selects realtime PTT; true selects voice notes only. */
bool dou_settings_voice_notes(void);
void dou_settings_set_voice_notes(bool on);
#if CONFIG_DOU_WAKEWORD_ENABLE
bool dou_settings_wakeword_on(void);   /* hands-free listening opt-in; default off */
#endif
/* With CONFIG_DOU_APP_VIEW, Apps in Settings (off until the person turns
 * them on): whether the gadget runs the apps their Dou makes. Changing it
 * restarts the gadget, so Dou learns of the app commands as it reconnects. */
bool dou_settings_apps_on(void);
void dou_settings_set_apps_on(bool on);
#if CONFIG_DOU_PHOTO_NOTES
/* Photo with voice notes (dou_photo_note.h); default off. */
bool dou_settings_photo_notes(void);
void dou_settings_set_photo_notes(bool on);
#endif

void dou_settings_wifi(char ssid[DOU_SSID_MAX + 1], char pass[DOU_PASS_MAX + 1]);
void dou_settings_hatch_host(char out[DOU_HOST_MAX + 1]);
void dou_settings_hatch_vm(char out[DOU_VM_MAX + 1]);
void dou_settings_hatch_token(char out[DOU_TOKEN_MAX + 1]);
size_t dou_settings_hatch_token_len(void);

void dou_settings_set_volume(int pct);
void dou_settings_set_speaker_on(bool on);
void dou_settings_set_mic_gain(int db);
void dou_settings_set_brightness(int pct);
void dou_settings_set_sleep_s(int secs);
void dou_settings_set_text_size(int size);
void dou_settings_set_wifi_on(bool on);
void dou_settings_set_ble_on(bool on);
#if CONFIG_DOU_WAKEWORD_ENABLE
void dou_settings_set_wakeword_on(bool on);
#endif
/* A network name is remembered first among the saved ones and joined now;
 * an empty ssid forgets every saved network. */
void dou_settings_set_wifi(const char *ssid, const char *pass);
/* False, changing nothing, for a server this build doesn't allow
 * (dou_server_hosts.h); empty means the default. */
bool dou_settings_set_hatch_host(const char *host);
void dou_settings_set_hatch_vm(const char *vm);
/* append=true adds to the stored token (for chunked BLE writes). */
esp_err_t dou_settings_set_hatch_token(const char *token, bool append);
