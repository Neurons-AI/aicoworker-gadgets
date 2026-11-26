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

// The Voice PE borrows Dou's voice session (components/dou/
// dou_chat_session.cpp) without the rest of Dou. These stand in for the Dou
// settings, state, Wi-Fi and Link hooks it calls: the session always uses
// Link's paired account, Link's Noise host and Link's preferred VM.

#include "voice_dou_chat.h"

#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "app.h"
#include "config_store.h"
#include "identity.h"
#include "dou_server_hosts.h"
#include "wifi_mgr.h"

#include "dou_link.h"
#include "dou_settings.h"
#include "dou_state.h"
#include "dou_wifi.h"
#if CONFIG_DOU_REALTIME_VOICE
#include "dou_voice_call.h"
#endif

// The session task's stack is in PSRAM, where reading flash would crash, so
// what it asks for comes from here rather than NVS.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool s_linked;
static char s_host[DOU_HOST_MAX + 1];

void voice_hatch_refresh(void) {
    char host[DOU_HOST_MAX + 1];
    if (!config_get_str("noise_host", host, sizeof(host)) || !dou_server_noise_allowed(host)) {
        host[0] = '\0';
    }
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_host, host, sizeof(s_host));
    portEXIT_CRITICAL(&s_lock);
    atomic_store(&s_linked, config_is_provisioned());
}

bool dou_link_hatch_linked(void) {
    return atomic_load(&s_linked);
}

// app_hatch_vm_credentials does its work on a task of its own.
bool dou_link_hatch_vm(const char *want_vm, char *vm_id, size_t id_cap, char *vm_name,
                        size_t name_cap, char **vm_token) {
    return atomic_load(&s_linked) &&
           app_hatch_vm_credentials(want_vm, vm_id, id_cap, vm_name, name_cap, vm_token);
}

bool dou_wifi_connected(void) {
    return wifi_mgr_is_connected();
}

// Empty: the session falls back to its default host.
void dou_settings_hatch_host(char out[DOU_HOST_MAX + 1]) {
    portENTER_CRITICAL(&s_lock);
    strlcpy(out, s_host, DOU_HOST_MAX + 1);
    portEXIT_CRITICAL(&s_lock);
}

// No VM of its own: Link's preferred one.
void dou_settings_hatch_vm(char out[DOU_VM_MAX + 1]) {
    out[0] = '\0';
}

// No token of its own: Link's account.
void dou_settings_hatch_token(char out[DOU_TOKEN_MAX + 1]) {
    out[0] = '\0';
}

size_t dou_settings_hatch_token_len(void) {
    return 0;
}

// No screen to show replies on: anything played goes to the speaker.
bool dou_settings_speaker_on(void) {
    return true;
}

// Dou's default page, for boards that never show reply text. A board that
// does defines its own.
__attribute__((weak)) void dou_state_page(const char *text, int *cols, int *lines) {
    (void)text;
    *cols = 16;
    *lines = 2;
}

#if CONFIG_DOU_REALTIME_VOICE
// The call names Link's node as its device (a RAM string set at boot).
const char *dou_voice_call_device_id(void) {
    return identity_node_id();
}
#endif
