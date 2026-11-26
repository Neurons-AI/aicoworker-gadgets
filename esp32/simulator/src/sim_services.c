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

#include "sim_services.h"

#include <stdio.h>
#include <string.h>

#include "dou_console.h"
#include "dou_audio.h"
#include "dou_input.h"
#include "dou_menu.h"
#include "dou_settings.h"
#include "dou_settings_ui.h"
#include "dou_voice.h"

/* The preview drives modes directly; it has no live call transport. */
static bool s_in_call;
bool dou_voice_in_call(void) { return s_in_call; }
bool dou_voice_reply_waiting(void) { return false; }
dou_voice_view_t dou_voice_call_view(void) { return s_in_call ? DOU_VOICE_VIEW_SPEAKING : DOU_VOICE_VIEW_NONE; }

#define SIM_DEFAULT_NAME "DouGadget-SIM001"
#define SIM_DEFAULT_SSID "Dou Simulator"

static dou_wifi_status_t s_wifi = {
    .state = DOU_WIFI_CONNECTED,
    .ssid = SIM_DEFAULT_SSID,
    .ip = "192.0.2.2",
    .rssi = -45,
};
static dou_ble_status_t s_ble = {
    .state = DOU_BLE_CONNECTED,
    .secure = true,
    .name = SIM_DEFAULT_NAME,
};
static dou_hatch_status_t s_chat = {
    .state = DOU_HATCH_REACHABLE,
};
static dou_link_state_t s_link = DOU_LINK_ONLINE;
static int s_brightness = 75;
static int s_volume = 70;
static bool s_speaker = true;

static void copy_text(char *out, size_t cap, const char *text)
{
    if (!cap) {
        return;
    }
    if (!text) {
        text = "";
    }
    size_t n = strlen(text);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, text, n);
    out[n] = '\0';
}

void sim_services_reset(void)
{
    s_wifi = (dou_wifi_status_t){
        .state = DOU_WIFI_CONNECTED,
        .ssid = SIM_DEFAULT_SSID,
        .ip = "192.0.2.2",
        .rssi = -45,
    };
    s_ble = (dou_ble_status_t){
        .state = DOU_BLE_CONNECTED,
        .secure = true,
        .name = SIM_DEFAULT_NAME,
    };
    s_chat = (dou_hatch_status_t){
        .state = DOU_HATCH_REACHABLE,
    };
    s_link = DOU_LINK_ONLINE;
    s_brightness = 75;
    s_volume = 70;
    s_speaker = true;
    s_in_call = false;
}

void sim_services_set_wifi(dou_wifi_state_t state, const char *ssid)
{
    s_wifi.state = state;
    copy_text(s_wifi.ssid, sizeof(s_wifi.ssid), ssid);
    copy_text(s_wifi.ip, sizeof(s_wifi.ip), state == DOU_WIFI_CONNECTED ? "192.0.2.2" : "");
    s_wifi.rssi = state == DOU_WIFI_CONNECTED ? -45 : 0;
    s_wifi.detail[0] = '\0';
}

void sim_services_set_ble(dou_ble_state_t state, const char *name, uint32_t passkey)
{
    s_ble.state = state;
    s_ble.passkey = passkey;
    s_ble.secure = state == DOU_BLE_CONNECTED;
    copy_text(s_ble.name, sizeof(s_ble.name), name);
}

void sim_services_set_paired(bool paired)
{
    if (!paired) {
        s_chat.state = DOU_HATCH_NOT_SET;
        s_chat.detail[0] = '\0';
    } else if (s_chat.state == DOU_HATCH_NOT_SET) {
        s_chat.state = DOU_HATCH_REACHABLE;
    }
}

void sim_services_set_chat_status(dou_hatch_state_t state, const char *detail)
{
    s_chat.state = state;
    copy_text(s_chat.detail, sizeof(s_chat.detail), detail);
}

void sim_services_set_link_state(dou_link_state_t state)
{
    s_link = state;
}

void sim_services_set_brightness(int pct)
{
    if (pct < 10) {
        pct = 10;
    } else if (pct > 100) {
        pct = 100;
    }
    s_brightness = pct;
}

void sim_services_set_speaker(bool on)
{
    s_speaker = on;
}

int dou_settings_brightness(void)
{
    return s_brightness;
}

int dou_settings_volume(void) { return s_volume; }
void dou_settings_set_volume(int pct) { s_volume = pct; }
void dou_audio_set_volume(int pct) { (void)pct; }

bool dou_settings_speaker_on(void)
{
    return s_speaker;
}

void dou_settings_set_brightness(int pct)
{
    sim_services_set_brightness(pct);
}

void dou_settings_set_speaker_on(bool on)
{
    sim_services_set_speaker(on);
}

/* Text size: the screen's default until someone picks one, as on a board. */
static int s_text_default = DOU_TEXT_SMALL;
int dou_settings_text_size(void) { return s_text_default; }
void dou_settings_set_text_size_default(int size) { s_text_default = size; }

void dou_wifi_status(dou_wifi_status_t *out)
{
    if (out) {
        *out = s_wifi;
    }
}

bool dou_wifi_connected(void)
{
    return s_wifi.state == DOU_WIFI_CONNECTED;
}

void dou_ble_status(dou_ble_status_t *out)
{
    if (out) {
        *out = s_ble;
    }
}

void dou_hatch_status(dou_hatch_status_t *out)
{
    if (out) {
        *out = s_chat;
    }
}

/* The latest-reply harness links the production text helpers. */
#if !CONFIG_DOU_LATEST_REPLY
void dou_chat_set_caption_px(int width_px, int (*glyph_px)(uint32_t letter, uint32_t next))
{
    (void)width_px;
    (void)glyph_px;
}
#endif

dou_link_state_t dou_link_state(void)
{
    return s_link;
}

void dou_settings_ui_build(lv_obj_t *tile)
{
    lv_obj_t *label = lv_label_create(tile);
    lv_label_set_text(label, "Settings unavailable in preview");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void dou_settings_ui_tick(bool visible)
{
    (void)visible;
}

bool dou_settings_ui_in_subpage(void)
{
    return false;
}

void dou_menu_key(dou_menu_key_t key)
{
    (void)key;
}

bool dou_menu_is_open(void)
{
    return false;
}

void dou_menu_build(lv_obj_t *parent, int w, int h)
{
    (void)parent;
    (void)w;
    (void)h;
}

bool dou_menu_tick(float now)
{
    (void)now;
    return false;
}

void dou_menu_close(void)
{
}

bool dou_console_write(const void *buf, size_t n)
{
    if (!n) {
        return true;
    }
    return buf && fwrite(buf, 1, n, stdout) == n && fflush(stdout) == 0;
}

void dou_input_touch_talk(bool pressed)
{
    (void)pressed;
}

void sim_services_set_call(bool on)
{
    s_in_call = on;
}

#if CONFIG_DOU_UI_THEME
static dou_theme_mode_t s_ui_theme;
dou_theme_mode_t dou_settings_theme(void) {return s_ui_theme;}
esp_err_t dou_settings_set_theme(dou_theme_mode_t mode) {s_ui_theme=mode;return ESP_OK;}
#endif
