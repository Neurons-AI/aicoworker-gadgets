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

#include "dou_chat_priv.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "dou_link.h"
#include "dou_settings.h"
#include "dou_wifi.h"

/* Connection state as last reported by the session task (dou_chat_session.cpp). */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static dou_hatch_state_t s_result = DOU_HATCH_UNTESTED;
static char s_detail[48];
static char s_vm_id[64];

void dou_hatch_report(dou_hatch_state_t state, const char *detail)
{
    portENTER_CRITICAL(&s_lock);
    s_result = state;
    strlcpy(s_detail, detail, sizeof(s_detail));
    portEXIT_CRITICAL(&s_lock);
}

void dou_hatch_report_vm(const char *vm_id)
{
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_vm_id, vm_id, sizeof(s_vm_id));
    portEXIT_CRITICAL(&s_lock);
}

bool dou_hatch_configured(void)
{
#if CONFIG_DOU_AICW_BRIDGE
    char host[DOU_HOST_MAX + 1];
    aicw_host_get(host);
    return host[0] != '\0';
#else
    return dou_settings_hatch_token_len() > 0 || dou_link_hatch_linked();
#endif
}

void dou_hatch_test(void)
{
    if (!dou_hatch_configured() || !dou_wifi_connected()) {
        return;
    }
    dou_hatch_report(DOU_HATCH_TESTING, "Connecting...");
    dou_hatch_chat_connect();
}

void dou_hatch_config_changed(void)
{
    dou_hatch_report(DOU_HATCH_UNTESTED, "");
    dou_hatch_report_vm("");
    dou_hatch_chat_forget();
}

void dou_hatch_status(dou_hatch_status_t *out)
{
    if (!dou_hatch_configured()) {
        out->state = DOU_HATCH_NOT_SET;
#if CONFIG_DOU_AICW_BRIDGE
        strlcpy(out->detail, "Set >aicw_host=ip:port", sizeof(out->detail));
#else
        strlcpy(out->detail, "Pair in the AICW app", sizeof(out->detail));
#endif
        out->vm_id[0] = '\0';
        return;
    }
    portENTER_CRITICAL(&s_lock);
    out->state = s_result;
    strlcpy(out->detail, s_detail, sizeof(out->detail));
    strlcpy(out->vm_id, s_vm_id, sizeof(out->vm_id));
    portEXIT_CRITICAL(&s_lock);
    if (!dou_wifi_connected() && out->state != DOU_HATCH_TESTING) {
        out->state = DOU_HATCH_OFFLINE;
        strlcpy(out->detail, "Waiting for Wi-Fi", sizeof(out->detail));
    } else if (out->state == DOU_HATCH_UNTESTED && !out->detail[0]) {
        strlcpy(out->detail, "Connects when you talk", sizeof(out->detail));
    }
}

const char *dou_hatch_state_name(dou_hatch_state_t state)
{
    switch (state) {
    case DOU_HATCH_NOT_SET: return "Not set up";
    case DOU_HATCH_OFFLINE: return "Offline";
    case DOU_HATCH_UNTESTED: return "Saved";
    case DOU_HATCH_TESTING: return "Connecting";
    case DOU_HATCH_REACHABLE: return "Connected";
    case DOU_HATCH_UNREACHABLE: return "Can't connect";
    }
    return "";
}
