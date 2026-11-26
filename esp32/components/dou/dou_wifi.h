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
#include <stdint.h>

#include "esp_err.h"

#include "dou_settings.h"

/*
 * Wi-Fi station driven by dou_settings: joins whichever saved network is in
 * range, reconnects with backoff, and runs scans for the settings screen. All
 * calls are safe from any task; status is copied out for the UI to poll.
 */

typedef enum {
    DOU_WIFI_OFF,
    DOU_WIFI_NO_NETWORK,     /* on, but nothing saved */
    DOU_WIFI_CONNECTING,
    DOU_WIFI_CONNECTED,
    DOU_WIFI_FAILED,         /* gave up (e.g. wrong password) */
    DOU_WIFI_NOT_NEARBY,     /* none of the saved networks showed up in a scan; looking again less often */
} dou_wifi_state_t;

typedef struct {
    dou_wifi_state_t state;
    char ssid[DOU_SSID_MAX + 1];
    char ip[16];
    int rssi;
    char detail[40];          /* human-readable reason while connecting/failed */
} dou_wifi_status_t;

typedef struct {
    char ssid[DOU_SSID_MAX + 1];
    int8_t rssi;
    bool secure;
} dou_wifi_ap_t;

#define DOU_WIFI_SAVED_MAX 8
typedef struct {
    char ssid[DOU_SSID_MAX + 1];
    bool hidden;              /* joined by name; doesn't show in scans */
} dou_wifi_saved_t;

esp_err_t dou_wifi_start(void);
/* Re-reads on/off and credentials from settings and (re)connects. */
void dou_wifi_apply(void);
void dou_wifi_status(dou_wifi_status_t *out);
bool dou_wifi_connected(void);
typedef enum {
    DOU_WIFI_FULL,   /* the mode from before: no modem sleep, but for BLE coexistence */
    DOU_WIFI_DOZE,   /* modem sleep between DTIM beacons: a few hundred ms extra latency */
    DOU_WIFI_REST,   /* over several beacons, and Link's session polls less: up to a second */
} dou_wifi_power_t;
void dou_wifi_power(dou_wifi_power_t level);
/* Holds Wi-Fi out of modem sleep whatever the level (a video stream: the
 * access point would otherwise hold each packet for the next beacon, up to
 * 100 ms). Calls nest; false releases one. */
void dou_wifi_keep_awake(bool on);
/* Leaves Wi-Fi, dropping Link's session and Dou's, until called with false,
 * which rejoins at once. */
void dou_wifi_nap(bool nap);

esp_err_t dou_wifi_scan(void);
bool dou_wifi_scanning(void);
/* Copies the latest results (strongest first); *gen changes when they do. */
int dou_wifi_scan_results(dou_wifi_ap_t *out, int max, uint32_t *gen);

/* Saved networks, most recently joined first. Returns the count. */
int dou_wifi_saved(dou_wifi_saved_t *out, int max);
/* Forgets one saved network, disconnecting first if it's the one in use. */
void dou_wifi_forget(const char *ssid);
