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

#include "dou_link.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

#include "dou_state.h"

static const char *TAG = "dou_link";
static const dou_link_ops_t *s_ops;
static volatile dou_link_state_t s_state = DOU_LINK_BOOT;

void dou_link_register(const dou_link_ops_t *ops)
{
    s_ops = ops;
}

void dou_link_set_state(dou_link_state_t state)
{
    if (state != s_state) {
        s_state = state;
        if (state == DOU_LINK_CONFIRM) {
            dou_state_poke();      /* keep the screen awake for the prompt */
        }
    }
}

dou_link_state_t dou_link_state(void)
{
    return s_state;
}

const char *dou_link_state_name(dou_link_state_t state)
{
    switch (state) {
    case DOU_LINK_BOOT: return "Starting";
    case DOU_LINK_UNPAIRED: return "Ready to pair";
    case DOU_LINK_PAIRING: return "App connected";
    case DOU_LINK_CONFIRM: return "Confirm pairing";
    case DOU_LINK_CONNECTING: return "Connecting";
    case DOU_LINK_ONLINE: return "Online";
    case DOU_LINK_OFFLINE: return "Offline";
    case DOU_LINK_ERROR: return "Error";
    }
    return "";
}

bool dou_link_hatch_linked(void)
{
    return s_ops && s_ops->hatch_linked && s_ops->hatch_linked();
}

const char *dou_link_sdk_token(void)
{
    return s_ops && s_ops->sdk_token ? s_ops->sdk_token() : NULL;
}

bool dou_link_sdk_token_set(const char *token)
{
    return s_ops && s_ops->sdk_token_set && s_ops->sdk_token_set(token);
}

bool dou_link_sdk_token_changed(void)
{
    return s_ops && s_ops->sdk_token_changed && s_ops->sdk_token_changed();
}

bool dou_link_hatch_vm(const char *want_vm, char *vm_id, size_t id_cap, char *vm_name, size_t name_cap,
                        char **vm_token)
{
    return s_ops && s_ops->hatch_vm && s_ops->hatch_vm(want_vm, vm_id, id_cap, vm_name, name_cap, vm_token);
}

/* Longer than a press takes to be reported (debouncing, a touch read, the input
 * task's poll), so a press reported now began less than this long ago. */
#define CONFIRM_SETTLE_US (250 * 1000)

bool dou_link_confirm_press(int64_t released_us)
{
    if (!s_ops || !s_ops->talk_press) {
        return false;
    }
    int64_t after = esp_timer_get_time() - CONFIRM_SETTLE_US;
    return s_ops->talk_press(released_us > after ? released_us : after);
}

bool dou_link_talk_press(void)
{
    return dou_link_confirm_press(0);
}

void dou_link_reset_setup(void)
{
    if (s_ops && s_ops->reset_setup) {
        s_ops->reset_setup();
    }
}

bool dou_link_wifi_get(char *ssid, char *pass)
{
    if (!s_ops || !s_ops->wifi_get) {
        return false;
    }
    s_ops->wifi_get(ssid, pass);
    return true;
}

bool dou_link_wifi_set(const char *ssid, const char *pass)
{
    if (!s_ops || !s_ops->wifi_set) {
        return false;
    }
    s_ops->wifi_set(ssid, pass);
    return true;
}

bool dou_link_ble_started(void)
{
    return s_ops && s_ops->ble_started && s_ops->ble_started();
}

bool dou_link_req_ready(void)
{
    return s_ops && s_ops->req_ready && s_ops->req_ready();
}

int64_t dou_link_req_open(const char *verb, const char *path, const char *const *headers, bool end_body,
                           dou_link_req_cb cb, void *ctx)
{
    return s_ops && s_ops->req_open ? s_ops->req_open(verb, path, headers, end_body, cb, ctx) : 0;
}

bool dou_link_req_send(int64_t id, const void *data, size_t len, bool end_body, int wait_ms)
{
    return s_ops && s_ops->req_send && s_ops->req_send(id, data, len, end_body, wait_ms);
}

void dou_link_req_cancel(int64_t id)
{
    if (s_ops && s_ops->req_cancel) {
        s_ops->req_cancel(id);
    }
}

#if CONFIG_DOU_WASM_APPS
size_t dou_link_req_max_body(void)
{
    /* Without Link's answer, what any board's session takes. */
    return s_ops && s_ops->req_max_body ? s_ops->req_max_body() : 4096;
}
#endif

void dou_link_ble_apply(void)
{
    if (s_ops && s_ops->ble_apply) {
        s_ops->ble_apply();
    }
}

/* ---- dou_wifi.h, backed by Link's wifi_mgr ---- */

esp_err_t dou_wifi_start(void)
{
    dou_wifi_apply();
    return ESP_OK;
}

void dou_wifi_apply(void)
{
    if (s_ops && s_ops->wifi_apply) {
        s_ops->wifi_apply();
    }
}

void dou_wifi_status(dou_wifi_status_t *out)
{
    if (s_ops && s_ops->wifi_status) {
        s_ops->wifi_status(out);
        return;
    }
    memset(out, 0, sizeof(*out));
    out->state = DOU_WIFI_OFF;
    strlcpy(out->detail, "Starting...", sizeof(out->detail));
}

bool dou_wifi_connected(void)
{
    dou_wifi_status_t w;
    dou_wifi_status(&w);
    return w.state == DOU_WIFI_CONNECTED;
}

static dou_wifi_power_t s_power_level = DOU_WIFI_FULL;
static wifi_ps_type_t s_power_before;   /* none, or modem sleep for BLE coexistence */
static int s_awake_holds;               /* dou_wifi_keep_awake */
static portMUX_TYPE s_power_lock = portMUX_INITIALIZER_UNLOCKED;

/* What the level (and any hold) asks of the radio. */
static wifi_ps_type_t power_ps(void)
{
    portENTER_CRITICAL(&s_power_lock);
    dou_wifi_power_t level = s_power_level;
    bool held = s_awake_holds > 0;
    portEXIT_CRITICAL(&s_power_lock);
    /* Resting, the radio wakes for every tenth beacon (the listen interval
     * wifi_mgr joins with, about 1 s), not every DTIM; replies can wait that
     * long. */
    return held                      ? WIFI_PS_NONE
           : level == DOU_WIFI_REST ? WIFI_PS_MAX_MODEM
           : level == DOU_WIFI_DOZE ? WIFI_PS_MIN_MODEM
                                     : s_power_before;
}

void dou_wifi_power(dou_wifi_power_t level)
{
    if (level == s_power_level) {
        return;
    }
    if (s_ops && s_ops->power_save) {
        s_ops->power_save(level == DOU_WIFI_REST);
    }
    if (s_power_level == DOU_WIFI_FULL && s_awake_holds == 0 && esp_wifi_get_ps(&s_power_before) != ESP_OK) {
        s_power_before = WIFI_PS_NONE;   /* as wifi_mgr starts it */
    }
    portENTER_CRITICAL(&s_power_lock);
    s_power_level = level;
    portEXIT_CRITICAL(&s_power_lock);
    esp_wifi_set_ps(power_ps());
}

void dou_link_ble_quiet(bool quiet)
{
    static int holds;
    portENTER_CRITICAL(&s_power_lock);
    int was = holds;
    holds += quiet ? 1 : (holds > 0 ? -1 : 0);
    int now = holds;
    portEXIT_CRITICAL(&s_power_lock);
    if ((was == 0) != (now == 0) && s_ops && s_ops->ble_quiet) {
        s_ops->ble_quiet(now > 0);
    }
}

void dou_wifi_keep_awake(bool on)
{
    wifi_ps_type_t was = WIFI_PS_NONE;
    esp_wifi_get_ps(&was);
    portENTER_CRITICAL(&s_power_lock);
    bool first = on && s_awake_holds == 0;
    s_awake_holds += on ? 1 : (s_awake_holds > 0 ? -1 : 0);
    portEXIT_CRITICAL(&s_power_lock);
    if (first && s_power_level == DOU_WIFI_FULL) {
        s_power_before = was;   /* what FULL means once the hold goes */
    }
    esp_err_t err = esp_wifi_set_ps(power_ps());
    ESP_LOGI(TAG, "wifi power save %d -> %d%s (%s)", (int)was, (int)power_ps(), on ? ", held awake" : "",
             esp_err_to_name(err));
}

void dou_wifi_nap(bool nap)
{
    if (s_ops && s_ops->wifi_nap) {
        s_ops->wifi_nap(nap);
    }
}

esp_err_t dou_wifi_scan(void)
{
    return s_ops && s_ops->wifi_scan ? s_ops->wifi_scan() : ESP_ERR_INVALID_STATE;
}

bool dou_wifi_scanning(void)
{
    return s_ops && s_ops->wifi_scanning && s_ops->wifi_scanning();
}

int dou_wifi_scan_results(dou_wifi_ap_t *out, int max, uint32_t *gen)
{
    if (s_ops && s_ops->wifi_scan_results) {
        return s_ops->wifi_scan_results(out, max, gen);
    }
    *gen = 0;
    return 0;
}

int dou_wifi_saved(dou_wifi_saved_t *out, int max)
{
    return s_ops && s_ops->wifi_saved ? s_ops->wifi_saved(out, max) : 0;
}

void dou_wifi_forget(const char *ssid)
{
    if (s_ops && s_ops->wifi_forget) {
        s_ops->wifi_forget(ssid);
    }
}

int dou_link_wifi_proxy_get(const char *ssid, network_proxy_config_t *out)
{
    return s_ops && s_ops->wifi_proxy_get ? s_ops->wifi_proxy_get(ssid, out) : -1;
}

bool dou_link_wifi_proxy_set(const char *ssid, const network_proxy_config_t *proxy)
{
    return s_ops && s_ops->wifi_proxy_set && s_ops->wifi_proxy_set(ssid, proxy);
}
