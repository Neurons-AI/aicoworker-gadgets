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

#include "dou_input.h"

#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#include "esp_sleep.h"
#endif

#include "dou_battery.h"
#include "dou_ble.h"
#include "dou_board.h"
#include "dou_chat.h"
#if CONFIG_DOU_HATCH
#include "dou_chat_priv.h"
#endif
#include "dou_options.h"
#include "dou_console.h"
#include "dou_link.h"
#include "dou_mem.h"
#include "dou_motion.h"
#include "dou_menu.h"
#if CONFIG_DOU_MIRROR
#include "dou_mirror.h"
#endif
#include "dou_settings.h"
#if CONFIG_DOU_UI_THEME
#include "dou_theme.h"
#endif
#if !CONFIG_DOU_UI_FREEINK
#include "dou_settings_ui.h"   /* LVGL only; e-paper builds use FreeInk */
#endif
#include "dou_state.h"
#include "dou_ui.h"
#if CONFIG_DOU_LATEST_REPLY
#include "dou_latest_reply.h"
#endif
#include "dou_voice.h"
#include "dou_wakeword_glue.h"
#include "dou_wifi.h"
#if CONFIG_DOU_VIDEO
#include "dou_video.h"
#endif
#if CONFIG_DOU_BOARD_SENSECAP_WATCHER
#include "dou_review.h"
#include "dou_wheel.h"
#endif
#if CONFIG_DOU_APP_VIEW
#include "dou_app_view.h"
#endif
#if CONFIG_DOU_WATCHER_CAMERA
#include "boards/watcher_camera.h"
#endif

static atomic_uint s_touch_talk_events;

void dou_input_touch_talk(bool pressed)
{
    atomic_fetch_or(&s_touch_talk_events, pressed ? DOU_BTN_TALK_PRESS : DOU_BTN_TALK_RELEASE);
}

static const char *TAG = "dou_input";

#define POLL_MS 10
#define REST_POLL_MS 50        /* screen off and paused: still quick to wake */
#define REST_WAIT_MS 1000      /* paused, buttons that interrupt: the rest waits this long */
#define NAP_WAIT_MS 10000      /* ... and Wi-Fi napping */
#define POWER_MS 2000          /* refresh battery */
#define REST_POWER_MS 10000    /* ... while paused */
#define WIFI_NAP_MS (2 * 60 * 1000)   /* low power this long: Wi-Fi off until it ends */
#define DOUBLE_TICKS 35        /* 350 ms: a second aux press within this toggles phone setup */

#define GOODBYE_MS 1500        /* let the goodbye animation play */
#define HINT_TICKS 60          /* 0.6 s: warn that holding powers off */
#define LONG_TICKS 150         /* 1.5 s: power off */
#define SLEEP_CHECK_MS 100

#define SERIAL_RX 1024         /* the driver drops what doesn't fit, so a console line must */
#define SERIAL_LINE 1024
#define CHAT_MAX (192 * 1024)  /* a typed message, assembled from "chat+=" lines */

static QueueHandle_t s_queue;
static bool (*s_console_hook)(const char *line);
static TaskHandle_t s_input;
#if CONFIG_DOU_MIRROR
void dou_input_inject(unsigned events)
{
    atomic_fetch_or(&s_touch_talk_events, events);
    if (s_input) xTaskNotifyGive(s_input);
}
#endif
static bool s_talk_down;
static int64_t s_talk_released_us;   /* when the talk control was last let go */
#if CONFIG_DOU_VOICE_NOTE_TOGGLE
static atomic_bool s_note_toggled;   /* a toggled voice note is recording, talk released */

void dou_input_note_toggled(bool on)
{
    atomic_store(&s_note_toggled, on);
}
#define NOTE_TOGGLED() atomic_load(&s_note_toggled)
#else
#define NOTE_TOGGLED() false
#endif
/* A push-to-talk press the voice task hasn't taken up yet: the speaker
 * button already ends the call it will start. Each such DOWN sets a mark of
 * its own and carries it; the voice task clears a mark only by that id, so
 * a press posted while it clears an older one keeps its mark. 0: none. */
static atomic_uint s_call_pending;
static atomic_uint s_mark_seq;
static atomic_uint s_call_epoch;
uint32_t dou_input_call_epoch(void) { return atomic_load(&s_call_epoch); }
void dou_input_clear_call_pending(uint32_t mark)
{
    if (mark) {
        atomic_compare_exchange_strong(&s_call_pending, &mark, 0);
    }
}
static bool s_cpu_low;      /* display stopped and the CPU allowed to sleep */
static volatile bool s_power_off_requested;
static volatile bool s_restart_requested;
static bool s_cpu_pd;       /* dou_input_power_init() holds the CPU's retention memory */
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_awake;   /* with s_cpu_pd: held whenever !s_cpu_low */
static bool s_awake_held;
#endif
static volatile bool s_nap_now;   /* ">nap": asleep, as if on battery, nap without waiting WIFI_NAP_MS */

static void post(dou_ptt_t type, bool wake)
{
    dou_input_event_t ev = { .type = type, .wake = wake, .epoch = dou_input_call_epoch(),
                             .at_us = esp_timer_get_time() };
    bool pending = type == DOU_PTT_DOWN && dou_voice_ptt_enabled() && !dou_voice_in_call();
    if (pending) {
        uint32_t mark;
        do {
            mark = atomic_fetch_add(&s_mark_seq, 1) + 1;
        } while (!mark);
        ev.mark = mark;
        atomic_store(&s_call_pending, mark);   /* before the send, so the speaker button sees it */
    }
    ESP_LOGI(TAG, "PTT %s%s", type == DOU_PTT_DOWN ? "down" : type == DOU_PTT_UP ? "up" : "end call",
             wake ? " (waking)" : "");
    if (xQueueSend(s_queue, &ev, pdMS_TO_TICKS(20)) != pdTRUE) {
        /* A lost edge cannot leave a live microphone open. Invalidate the
         * entire gesture, including queued older DOWNs, without another send. */
        atomic_fetch_add(&s_call_epoch, 1);
        atomic_store(&s_call_pending, 0);
        ESP_LOGW(TAG, "input queue full: call gesture invalidated");
    }
}

static bool update_power(void);

static void power_off(void)
{
    if (!dou_board->power_off) {
        return;   /* a physical switch powers it off */
    }
    ESP_LOGI(TAG, "shutting down");
    dou_voice_shutdown(3000);   /* bounded best effort; never drop power before requesting end-call */
    dou_state_set_asleep(false);
    update_power();
    dou_state_set_progress(0);
    dou_state_set_level(0);
    dou_state_set_mode(DOU_MODE_OFF);
    dou_state_set_caption("GOODBYE!");
    vTaskDelay(pdMS_TO_TICKS(GOODBYE_MS));
#if CONFIG_PM_ENABLE
    /* Automatic light sleep leaves the timer armed, and a deep sleep would
     * wake on it at once (with s_cpu_pd light sleep stays configured). */
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
#endif
    esp_err_t err = dou_board->power_off();
    /* Only reached if the board couldn't power off. */
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGE(TAG, "power-off failed (%s)", esp_err_to_name(err));
    dou_voice_cancel_shutdown();
    dou_state_set_mode(DOU_MODE_IDLE);
    dou_state_set_caption("COULDN'T POWER OFF");
}

static void restart(void)
{
    ESP_LOGI(TAG, "restarting");
    dou_voice_shutdown(3000);   /* bounded best effort, as before a power-off */
    dou_state_set_asleep(false);
    update_power();
    dou_state_set_progress(0);
    dou_state_set_level(0);
    dou_state_set_caption("RESTARTING...");
    vTaskDelay(pdMS_TO_TICKS(GOODBYE_MS));
    esp_restart();
}

static void set_asleep(bool asleep, const char *why)
{
    if (asleep && NOTE_TOGGLED()) {
        return;   /* never hide a recording note */
    }
    if (asleep != dou_state_asleep()) {
        ESP_LOGI(TAG, "%s (%s)", asleep ? "sleeping" : "waking", why);
        dou_state_set_asleep(asleep);
#if CONFIG_DOU_BOARD_SENSECAP_WATCHER
        if (asleep) {
            dou_review_close();   /* auto-sleep too: waking shows the live caption */
        }
#endif
        if (s_input) {
            xTaskNotifyGive(s_input);   /* out of wait_buttons() */
        }
        if (!asleep && !dou_wifi_connected()) {
            dou_wifi_apply();   /* the screen says reconnecting: don't sit out the backoff */
        }
    }
}

static void toggle_phone_setup(void)
{
    bool on = !dou_settings_ble_on();
    ESP_LOGI(TAG, "phone setup %s", on ? "on" : "off");
    dou_settings_set_ble_on(on);
    dou_ble_status_t b;
    dou_ble_status(&b);
    if (on && b.name[0]) {
        /* The setup name stays up: it is the instruction for pairing. */
        dou_state_set_caption("PHONE SETUP: %s", b.name);
    } else if (on) {
        dou_state_set_caption("PHONE SETUP ON");
    } else {
        /* OFF is a confirmation, not a state worth keeping on screen. */
        dou_state_set_hint("PHONE SETUP OFF");
    }
}

/*
 * Aux button: short press sleeps, a 1.5 s hold powers off, any press wakes.
 * Two quick presses toggle BLE phone setup, so the sleep waits a moment to
 * see whether a second press follows.
 */
static void aux_button(bool pressed, bool edge)
{
    static int held;
    static bool swallow;
    static bool hinted;
    static int sleep_in;    /* ticks until a pending single press sleeps */
    static char saved_caption[64];
    static bool saved_hint;

    if (sleep_in && --sleep_in == 0) {
        set_asleep(true, dou_board->aux_button);
    }
    if (edge && pressed) {
        held = 0;
        hinted = false;
        swallow = dou_state_asleep();
        if (swallow) {
            set_asleep(false, dou_board->aux_button);
        } else if (sleep_in) {
            sleep_in = 0;
            swallow = true;
            toggle_phone_setup();
        }
        return;
    }
    if (pressed && !swallow) {
        if (!dou_board->power_off) {
            return;   /* no hold to power off: a press only sleeps */
        }
        held++;
        if (held == HINT_TICKS) {
            uint32_t v = UINT32_MAX;
            dou_state_caption(saved_caption, sizeof(saved_caption), &v);
            saved_hint = dou_state_hint_pending();
            dou_state_set_caption("HOLD TO POWER OFF");
            hinted = true;
        } else if (held == LONG_TICKS) {
            swallow = true;
            power_off();
        }
        return;
    }
    if (edge && !pressed && !swallow) {
        if (hinted) {
            /* Let go early: cancel, and put back what was there, a hint as a hint. */
            if (saved_hint) {
                dou_state_set_hint(saved_caption);
            } else {
                dou_state_set_caption("%s", saved_caption);
            }
        } else {
            sleep_in = DOUBLE_TICKS;
        }
    }
}

/* No touch: the aux button opens the menu and steps down it; any press wakes. */
static void menu_button(bool pressed, bool edge)
{
    if (!edge || !pressed) {
        return;
    }
    if (dou_state_asleep()) {
        set_asleep(false, dou_board->aux_button);
    } else if (!s_talk_down && !NOTE_TOGGLED()) {
        dou_state_poke();
        dou_menu_key(DOU_MENU_DOWN);
    }
}

static void aux_key(bool pressed, bool edge)
{
    if (dou_board->touch) {
        aux_button(pressed, edge);
    } else {
        menu_button(pressed, edge);
    }
}

/* Talk button: push-to-talk, or Select while the menu is open. Asleep, the
 * press wakes and is posted as a waking one: dou_voice records only if it's
 * still held once awake. A display-only board (no audio_init or mic_init)
 * runs no voice task to take presses off the queue, so there the press only
 * wakes, selects or confirms pairing. Pairing takes a press that began after
 * its prompt: only a press's own edge can confirm (`can_confirm`; not a press
 * reported after its hold, DOU_BTN_TALK_HELD or the Watcher's wheel), with
 * the time the control was last let go. */
static void talk_input(unsigned ev, bool can_confirm)
{
    bool talk_down = s_talk_down;
    bool voice = dou_board->audio_init || dou_board->mic_init;
    static bool swallow;
    /* A quick tap can latch press and release in the same poll, and a release
     * can land just before the next press; keep them ordered. */
    bool released = ev & DOU_BTN_TALK_RELEASE;
    if ((talk_down || swallow) && released) {
        if (talk_down) {
            post(DOU_PTT_UP, false);
        }
        talk_down = swallow = false;
        released = false;
    }
    if (!talk_down && !swallow && (ev & DOU_BTN_TALK_PRESS)) {
        if (can_confirm && dou_link_confirm_press(s_talk_released_us)) {
            /* Confirmed a AICW app pairing (Link's setup button). */
            dou_state_poke();
            swallow = true;
        } else if (dou_state_asleep()) {
            set_asleep(false, dou_board->talk_button);
            if (voice) {
                post(DOU_PTT_DOWN, true);
                talk_down = true;
            } else {
                swallow = true;
            }
        } else if (dou_menu_is_open()) {
            dou_state_poke();
            if (!dou_board->keyboard) dou_menu_key(DOU_MENU_SELECT);
            swallow = true;
        } else if (voice) {
            post(DOU_PTT_DOWN, false);
            talk_down = true;
        } else {
            dou_state_poke();
            swallow = true;
        }
    }
    if ((talk_down || swallow) && released) {
        if (talk_down) {
            post(DOU_PTT_UP, false);
        }
        talk_down = swallow = false;
    }
    s_talk_down = talk_down;
    if (ev & DOU_BTN_TALK_RELEASE) {
        s_talk_released_us = esp_timer_get_time();   /* after this event's press */
    }
}

static void talk_button(unsigned ev)
{
    talk_input(ev, !(ev & DOU_BTN_TALK_HELD));
}

/* Keyboard menus don't repurpose Space/GO as Select. Navigation wakes the
 * screen without accidentally changing a setting; Enter can confirm pairing. */
static void keyboard_buttons(unsigned ev)
{
    const unsigned mask = DOU_BTN_UP | DOU_BTN_DOWN | DOU_BTN_LEFT |
                          DOU_BTN_RIGHT | DOU_BTN_ENTER | DOU_BTN_ESCAPE;
    if (!(ev & mask) || s_talk_down || NOTE_TOGGLED()) return;
    if ((ev & DOU_BTN_ENTER) && dou_link_talk_press()) {
        dou_state_poke();
        return;
    }
    if (dou_state_asleep()) {
        set_asleep(false, "keyboard");
        return;
    }
    dou_state_poke();
    if (ev & DOU_BTN_ESCAPE) dou_menu_key(DOU_MENU_BACK);
    else if (ev & DOU_BTN_UP) dou_menu_key(DOU_MENU_UP);
    else if (ev & DOU_BTN_DOWN) dou_menu_key(DOU_MENU_DOWN);
    else if (ev & DOU_BTN_LEFT) dou_menu_key(DOU_MENU_LEFT);
    else if (ev & DOU_BTN_RIGHT) dou_menu_key(DOU_MENU_RIGHT);
    else if (ev & DOU_BTN_ENTER) dou_menu_key(DOU_MENU_SELECT);
}

#if CONFIG_DOU_BOARD_SENSECAP_WATCHER
static void watcher_buttons(unsigned ev)
{
    static dou_wheel_t wheel;
    bool down = wheel.down;
    if (ev & DOU_BTN_TALK_PRESS) down = true;
    else if (ev & DOU_BTN_TALK_RELEASE) down = false;   /* let go before the hold timer runs */
    bool confirmed = down && !wheel.down && dou_link_confirm_press(s_talk_released_us);
    bool asleep = dou_state_asleep();
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
#if CONFIG_DOU_WATCHER_CAMERA
    static bool sleep_pending;
    static uint32_t sleep_deadline;
    if (confirmed) sleep_pending = false;
    if (sleep_pending && (int32_t)(now - sleep_deadline) >= 0) {
        sleep_pending = false;
        set_asleep(true, "wheel click");
        asleep = true;
    }
    if (down && !wheel.down && sleep_pending) {
        sleep_pending = false;
        confirmed = true;   /* consume the second click without starting a note */
        dou_review_close();
        ESP_LOGI(TAG, "wheel double-click: camera preview/shutter");
        watcher_camera_preview_toggle();
    }
#endif
    dou_wheel_action_t action = dou_wheel_update(&wheel, down, now, asleep, confirmed);
    if (confirmed) dou_state_poke();
    if (action == DOU_WHEEL_WAKE) {
        set_asleep(false, "wheel click");
    } else if (action == DOU_WHEEL_TALK_DOWN) {
        dou_review_close();
        talk_input(DOU_BTN_TALK_PRESS, false);   /* the press's edge had its chance to confirm */
    }
    if ((ev & DOU_BTN_TALK_RELEASE) && wheel.down) {   /* pressed and let go in one poll */
        action = dou_wheel_update(&wheel, false, now, dou_state_asleep(), false);
    }
    if (action == DOU_WHEEL_TALK_UP) {
        talk_button(DOU_BTN_TALK_RELEASE);
    } else if (action == DOU_WHEEL_SLEEP) {
        dou_review_close();
#if CONFIG_DOU_WATCHER_CAMERA
        sleep_pending = true;
        sleep_deadline = now + 350;
#else
        set_asleep(true, "wheel click");
#endif
    }
    if ((ev & (DOU_BTN_REVIEW_PREV | DOU_BTN_REVIEW_NEXT)) && !wheel.down) {
#if CONFIG_DOU_WATCHER_CAMERA
        sleep_pending = false;
#endif
        set_asleep(false, "wheel review");
        dou_state_poke();
        if (dou_review_step(ev & DOU_BTN_REVIEW_NEXT ? 1 : -1)) {
            ESP_LOGI(TAG, "wheel: reviewing reply");
        } else if (dou_state_mode(NULL) == DOU_MODE_IDLE) {
            dou_state_set_caption("NO REPLY YET");   /* not over an error or a turn's caption */
        }
    }
    if (ev & DOU_BTN_TALK_RELEASE) {
        s_talk_released_us = esp_timer_get_time();   /* after this event's press */
    }
}
#endif

/* A pairing prompt wakes the screen and keeps it on; otherwise idle sleeps. */
static void check_sleep(void)
{
    dou_ble_status_t ble;
    dou_ble_status(&ble);
    bool prompt = ble.passkey || dou_link_state() == DOU_LINK_CONFIRM;
    if (prompt) {
        set_asleep(false, "pairing");
        return;
    }
    if (dou_voice_in_call()) return;
    int after = dou_settings_sleep_s();
    float mode_t;
    if (after && !dou_state_asleep() && dou_state_mode(&mode_t) == DOU_MODE_IDLE
        && dou_state_idle_secs() > after) {
        set_asleep(true, "auto-sleep");
    }
}

static void set_cpu_low(bool low)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = low ? CONFIG_XTAL_FREQ : CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = low,
    };
    if (s_cpu_pd) {
        /* Light sleep stays configured: turning it off would free the
         * retention memory. The lock keeps the chip out of it instead, taken
         * before the full clock and let go only after the low one. */
        pm.light_sleep_enable = true;
        if (!low && !s_awake_held) {
            esp_pm_lock_acquire(s_awake);
            s_awake_held = true;
        }
    }
    esp_err_t err = esp_pm_configure(&pm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "power management: %s", esp_err_to_name(err));
    }
    if (s_cpu_pd && low && s_awake_held && err == ESP_OK) {
        esp_pm_lock_release(s_awake);
        s_awake_held = false;
    }
#else
    (void)low;
#endif
}

void dou_input_power_init(void)
{
#if CONFIG_PM_ENABLE && CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP
    esp_err_t err = esp_sleep_cpu_retention_init();
    if (err == ESP_OK) {
        err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "dou_awake", &s_awake);
    }
    if (err == ESP_OK) {
        esp_pm_lock_acquire(s_awake);
        s_awake_held = true;
        const esp_pm_config_t pm = {
            .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
            .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
            .light_sleep_enable = true,
        };
        err = esp_pm_configure(&pm);
    }
    s_cpu_pd = err == ESP_OK;
    if (!s_cpu_pd) {
        if (s_awake) {
            if (s_awake_held) {
                esp_pm_lock_release(s_awake);
                s_awake_held = false;
            }
            esp_pm_lock_delete(s_awake);
            s_awake = NULL;
        }
        esp_sleep_cpu_retention_deinit();
        ESP_LOGW(TAG, "CPU stays powered in light sleep: no retention memory (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "CPU powers down in light sleep: retention memory held");
    }
#endif
    dou_battery_note_cpu_pd(s_cpu_pd);
}

bool dou_input_cpu_pd(void)
{
    return s_cpu_pd;
}

/*
 * On battery with the screen dark and voice resting: stop the display and let
 * the CPU drop to the crystal clock and light-sleep between polls, or until a
 * button interrupts (wait_buttons). Returns true while the display is stopped.
 * With a USB host attached (">nap" on the bench) the CPU stays at full speed:
 * at the crystal clock a long line of serial output can stall the USB console
 * until the host reopens the port.
 */
static bool update_power(void)
{
    static bool paused;
    bool pause = dou_board->display_pause && dou_state_on_battery() && dou_state_asleep()
                 && dou_ui_dark() && dou_voice_resting();
#if CONFIG_DOU_MIRROR
    /* The viewer needs LVGL timers even while the physical screen is dark. */
    pause = pause && !dou_mirror_connected();
#endif
    bool want_low = pause && !dou_console_host();
    if (pause == paused && want_low == s_cpu_low) {
        return paused;
    }
    if (pause && !paused) {
        dou_board->display_pause(true);
    }
    if (want_low != s_cpu_low) {
        set_cpu_low(want_low);
    }
    if (!pause && paused) {
        dou_board->display_pause(false);
    }
    paused = pause;
    s_cpu_low = want_low;
    ESP_LOGI(TAG, "%s", s_cpu_low ? "low power: display paused"
                        : paused  ? "display paused (USB host: CPU at full speed)"
                                  : "full power");
    return paused;
}

/*
 * Low power for WIFI_NAP_MS: Wi-Fi off, since keeping it associated costs
 * more than the rest of the chip. It rejoins when the screen wakes or USB
 * power arrives; meanwhile nothing reaches the device over the network.
 * Not while a voice note recorded offline waits to go (for a while). Returns
 * true while napping.
 */
static bool update_wifi_nap(TickType_t now, bool paused)
{
    static TickType_t low_since;
    static bool napping;
    if (!s_cpu_low) {
        low_since = now;
    }
    if (!dou_state_asleep() && s_nap_now) {
        s_nap_now = false;
        dou_state_set_as_if_battery(false);
    }
    /* A gadget that talks to an AI computer keeps Wi-Fi (in modem sleep) while it sleeps:
     * with Wi-Fi off a press had to rejoin and reconnect first, and the question was saved
     * for later instead of answered ("SAVED, WILL TRY AGAIN" on battery). ">nap" still naps. */
    bool keep = false;
#if CONFIG_DOU_HATCH
    keep = dou_hatch_ready() || dou_hatch_configured();
#endif
    bool nap = paused && !dou_voice_notes_waiting()
               && (s_nap_now || (!keep && s_cpu_low && now - low_since >= pdMS_TO_TICKS(WIFI_NAP_MS)));
    if (nap != napping) {
        napping = nap;
        ESP_LOGI(TAG, "Wi-Fi %s", nap ? "napping" : "waking");
        dou_wifi_nap(nap);
    }
    return napping;
}

/* Independent POWER never goes through PTT, AUX's menu or its setup double
 * click. LONG is supplied by the board only after a validated hold. */
static bool s_power_down, s_power_swallow;

static void power_release(void)
{
    if (!s_power_down) return;
    s_power_down = false;
#if CONFIG_DOU_APP_VIEW
    /* With an app open, the power key closes it instead of sleeping the screen. */
    if (!s_power_swallow && dou_app_view_power()) {
        s_power_swallow = false;
        return;
    }
#endif
    if (!s_power_swallow) set_asleep(true, dou_board->power_button);
    s_power_swallow = false;
}

static void dedicated_buttons(unsigned ev)
{
    bool release = (ev & DOU_BTN_POWER_RELEASE) != 0;
    /* Preserve release-before-new-press ordering for latched boards. */
    if (s_power_down && release) {
        power_release();
        release = false;
    }
    if (!s_power_down && (ev & DOU_BTN_POWER_PRESS)) {
        s_power_down = true;
        s_power_swallow = dou_state_asleep();
        dou_state_poke();
        if (s_power_swallow) set_asleep(false, dou_board->power_button);
    }
    if (s_power_down && !s_power_swallow && (ev & DOU_BTN_POWER_LONG)) {
        s_power_swallow = true;   /* no sleep on release after shutdown/failure */
        if (s_talk_down) talk_button(DOU_BTN_TALK_RELEASE);
        power_off();
    }
    if (release) power_release();
#if CONFIG_DOU_BOARD_M5STACK_STOPWATCH_FAMILY && CONFIG_DOU_REALTIME_VOICE
    /* In a push-to-talk call, or with its press still on the way, the speaker
     * button (blue) hangs up, on the press, and wakes the screen. A quick
     * second press just after doesn't toggle the speaker. If the hang-up
     * can't be queued, post() moves the input epoch, which ends the call. */
    static int ignore_in;
    if (ignore_in) --ignore_in;
    if (ev & DOU_BTN_SPEAKER_PRESS) {
        if (dou_voice_in_call() || atomic_load(&s_call_pending)) {
            dou_state_poke();
            set_asleep(false, dou_board->speaker_button);
            post(DOU_PTT_END_CALL, false);
            ignore_in = DOUBLE_TICKS;
            return;
        }
        if (ignore_in) {
            return;
        }
    }
#endif
    if (ev & DOU_BTN_SPEAKER_PRESS) {
        dou_state_poke();
        if (dou_state_asleep()) {
            set_asleep(false, dou_board->speaker_button);   /* wake-only press */
        } else if (dou_state_mode(NULL) != DOU_MODE_OFF) {
            bool on = !dou_settings_speaker_on();
            dou_settings_set_speaker_on(on);
            if (dou_state_mode(NULL) == DOU_MODE_IDLE) {
                dou_state_set_hint(on ? "SPEAKER ON" : "SPEAKER OFF");
            }
        }
    }
}

static void input_task(void *arg)
{
    (void)arg;
    bool aux_down = false;
    bool paused = false;
    TickType_t checked = xTaskGetTickCount() - pdMS_TO_TICKS(SLEEP_CHECK_MS);
    TickType_t powered = xTaskGetTickCount() - pdMS_TO_TICKS(POWER_MS);

    for (;;) {
#if CONFIG_DOU_MOTION_GESTURES
        if (dou_motion_tick((uint32_t)(esp_timer_get_time() / 1000))) {
            set_asleep(false, "lift");
        }
#endif
        unsigned ev = dou_board->poll_buttons() | atomic_exchange(&s_touch_talk_events, 0);
#if CONFIG_DOU_VIDEO
        /* Any press leaves the video view (its release then does nothing). */
        const unsigned presses = DOU_BTN_TALK_PRESS | DOU_BTN_AUX_PRESS | DOU_BTN_POWER_PRESS |
                                 DOU_BTN_SPEAKER_PRESS | DOU_BTN_UP | DOU_BTN_DOWN | DOU_BTN_LEFT |
                                 DOU_BTN_RIGHT | DOU_BTN_ENTER | DOU_BTN_ESCAPE;
        if ((ev & presses) && dou_video_is_open()) {
            dou_video_close();
            ev &= ~(presses | DOU_BTN_POWER_LONG);
        }
#endif
#if CONFIG_DOU_APP_VIEW
        /* An open app takes the talk, speaker and aux buttons; the power key still closes it.
         * A toggled voice note that's recording keeps the talk key: its next press stops it. */
        unsigned keep = NOTE_TOGGLED() ? ev & (DOU_BTN_TALK_PRESS | DOU_BTN_TALK_RELEASE) : 0;
        ev = dou_app_view_buttons(ev & ~keep) | keep;
#endif
        if (ev & (DOU_BTN_TALK_PRESS | DOU_BTN_TALK_RELEASE)) {
            ESP_LOGI(TAG, "talk key:%s%s", ev & DOU_BTN_TALK_PRESS ? " press" : "",
                     ev & DOU_BTN_TALK_RELEASE ? " release" : "");
#if !CONFIG_DOU_BOARD_SENSECAP_WATCHER
            talk_button(ev);
#endif
        }
#if CONFIG_DOU_BOARD_SENSECAP_WATCHER
        watcher_buttons(ev);
#endif
        keyboard_buttons(ev);
        dedicated_buttons(ev);
        /* A latched key (the 1.75's PMU) can report press and release in the
         * same poll, and a release can land just before the next press; keep
         * them ordered, as talk_button does. */
        bool aux_press = ev & DOU_BTN_AUX_PRESS;
        bool aux_release = ev & DOU_BTN_AUX_RELEASE;
        bool aux_edge = false;
        if (aux_down && aux_release) {
            aux_down = false;
            aux_release = false;
            aux_edge = true;
            aux_key(false, true);
        }
        if (!aux_down && aux_press) {
            aux_down = aux_edge = true;
            aux_key(true, true);
            if (aux_release) {
                aux_down = false;
                aux_key(false, true);
            }
        }
        if (!aux_edge) {
            aux_key(aux_down, false);
        }

        if (s_power_off_requested) {
            s_power_off_requested = false;
            power_off();
        }
        if (s_restart_requested) {
            s_restart_requested = false;
            restart();
        }

        TickType_t now = xTaskGetTickCount();
        if (now - checked >= pdMS_TO_TICKS(SLEEP_CHECK_MS)) {
            checked = now;
            check_sleep();
        }

        if (now - powered >= pdMS_TO_TICKS(paused ? REST_POWER_MS : POWER_MS)) {
            powered = now;
            dou_power_t p = { .battery_pct = -1 };
            if (dou_board->read_power && dou_board->read_power(&p) == ESP_OK) {
                dou_state_set_power(&p);
                dou_battery_note_power(&p, dou_state_on_battery());
            }
        }

        paused = update_power();
        bool napping = update_wifi_nap(now, paused);
        dou_battery_note_state(dou_state_asleep(), s_cpu_low);
        /* Paused, anything but a button (a pairing prompt, an image) is
         * noticed within REST_WAIT_MS. Napping, nothing comes over the
         * network; USB power arriving is noticed within NAP_WAIT_MS. */
        if (paused && dou_board->wait_buttons) {
            int wait_ms = napping ? NAP_WAIT_MS : REST_WAIT_MS;
#if CONFIG_DOU_MOTION_GESTURES
            if (dou_motion_active()) wait_ms = (int)dou_motion_poll_ms();
#endif
            dou_board->wait_buttons(wait_ms);
        } else {
            vTaskDelay(pdMS_TO_TICKS(paused ? REST_POLL_MS : POLL_MS));
        }
    }
}

/* Reads the rest of a line into buf; false if it was too long (the rest is dropped). */
static bool read_line(char *buf, size_t cap)
{
    size_t len = 0;
    bool whole = true;
    uint8_t c;
    while (dou_console_getc(&c) && c != '\n') {
        if (c == '\r') {
            continue;
        }
        if (len < cap - 1) {
            buf[len++] = (char)c;
        } else {
            whole = false;
        }
    }
    buf[len] = '\0';
    return whole;
}

#if CONFIG_DOU_HATCH
#define CHAT_OVER_SERIAL "true"

/*
 * "chat+=TEXT" adds a piece of a message and "chat=TEXT" adds the last piece
 * and sends it (TEXT escaped: \n \r \t \\). Every line is acknowledged, and the
 * host waits for that before the next: the driver drops what it has no room for.
 */
static char *s_chat;        /* the message so far */
static size_t s_chat_len;

static void chat_line(char *piece, bool last, bool whole)
{
    size_t n = dou_hatch_unescape(piece);
    if (!s_chat) {
        s_chat = heap_caps_malloc(CHAT_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_chat_len = 0;
    }
    const char *err = !whole ? "LINE TOO LONG" : !s_chat ? "OUT OF MEMORY" : s_chat_len + n >= CHAT_MAX ? "TOO LONG" : NULL;
    if (err) {
        free(s_chat);
        s_chat = NULL;
        dou_hatch_console("error", err, NULL);
        return;
    }
    memcpy(s_chat + s_chat_len, piece, n);
    s_chat_len += n;
    s_chat[s_chat_len] = '\0';
    dou_hatch_console("ack", NULL, "\"bytes\":%u", (unsigned)s_chat_len);
    if (last) {
        dou_hatch_text_turn(s_chat);   /* frees it */
        s_chat = NULL;
    }
}

/* Drops a half-sent message and ends a typed turn. */
static void chat_cancel(void)
{
    free(s_chat);
    s_chat = NULL;
    dou_hatch_text_cancel();
}
#else
#define CHAT_OVER_SERIAL "false"   /* no PSRAM: replies come over Link, and only short ones */
#endif

/*
 * Bench: puts the face in a mode ("face=thinking") until the voice path or
 * another "face=" moves it on. "face=happy" goes back to idle with the happy
 * hop, as a finished turn does.
 */
static void set_face(const char *name)
{
    static const char *const modes[DOU_MODE_COUNT] = {
        [DOU_MODE_BOOT] = "boot",
        [DOU_MODE_IDLE] = "idle",
        [DOU_MODE_LISTENING] = "listening",
        [DOU_MODE_THINKING] = "thinking",
        [DOU_MODE_SPEAKING] = "speaking",
        [DOU_MODE_ERROR] = "error",
        [DOU_MODE_OFF] = "off",
    };
    dou_state_poke();
    bool happy = !strcmp(name, "happy");
    if (happy) {
        name = "idle";   /* where a finished turn hops to */
    }
    for (int m = 0; m < DOU_MODE_COUNT; m++) {
        if (!strcmp(name, modes[m])) {
            dou_state_set_mode(DOU_MODE_IDLE);   /* only idle leaves "off" */
            dou_state_set_mode((dou_mode_t)m);
            if (happy) {
                dou_state_make_happy();
            }
            return;
        }
    }
    printf("@face.error unknown face \"%s\": boot idle listening thinking speaking error off happy\n", name);
    fflush(stdout);
}

/*
 * Console-only commands; false for setup commands. Their buffers are taken
 * per command: without PSRAM, static ones would hold internal RAM for good.
 */
static bool console_command(char *line, bool whole)
{
#if CONFIG_DOU_HATCH
    if (whole && dou_options_console(line)) {
        dou_state_poke();
        dou_state_set_asleep(false);
        dou_state_nudge();
        return true;
    }
#endif
    if (whole && s_console_hook && s_console_hook(line)) {
        return true;
    }
#if CONFIG_DOU_MIRROR
    if (whole && (!strcmp(line, "mirror") || !strncmp(line, "mirror=", 7))) {
        dou_mirror_serial_session(line[6] ? atoi(line + 7) : 0);
        return true;
    }
#endif
#if CONFIG_DOU_BOARD_SENSECAP_WATCHER
    if (!strcmp(line, "review.next") || !strcmp(line, "review.prev")) {
        set_asleep(false, "reply review");
        dou_state_poke();
        dou_review_step(!strcmp(line, "review.next") ? 1 : -1);
        char page[DOU_CAPTION_MAX];
        int at = 0, pages = 0;
        dou_review_page(page, sizeof(page), &at, &pages);
        printf("@review {\"page\":%d,\"pages\":%d}\n", at, pages);
        fflush(stdout);
        return true;
    }
    if (!strcmp(line, "review.close")) {
        dou_review_close();
        return true;
    }
#endif
#if CONFIG_DOU_VIDEO
    /* The Phone camera item, for a bench without fingers: "video" opens the
     * view (asking the phone for a stream), "video=0" closes it. */
    if (!strcmp(line, "video") || !strcmp(line, "video=0")) {
        dou_state_poke();
        if (line[5]) {
            dou_video_close();
        } else {
            dou_video_open();
        }
        printf("@video %s\n", dou_video_is_open() ? "open" : "closed");
        fflush(stdout);
        return true;
    }
#endif
    if (!strcmp(line, "status")) {
        size_t cap = 1024;   /* long SSID, host and VM names escaped: past 512 */
        char *json = heap_caps_malloc(cap, DOU_BIG_CAPS);
        if (json) {
            dou_ble_status_json(json, cap);
            printf("@status {\"board\":\"%s\",\"chat\":%s,\"device\":%s}\n", dou_board->name,
                   CHAT_OVER_SERIAL, json);
            fflush(stdout);
            free(json);
        }
        return true;
    }
#if CONFIG_DOU_MOTION_GESTURES
    if (!strcmp(line, "motion")) {
        dou_motion_status();
        return true;
    }
#endif
    bool reset = !strcmp(line, "power.reset");
    if (reset || !strcmp(line, "power")) {
        if (reset) {
            dou_battery_reset();
        }
        const char *saved = dou_battery_saved_json();
        if (saved) {
            printf("@power.saved %s\n", saved);
        }
        size_t cap = 2048;   /* two dozen power locks */
        char *json = heap_caps_malloc(cap, DOU_BIG_CAPS);
        if (json) {
            dou_battery_json(json, cap);
            printf("@power %s\n", json);
            free(json);
        }
        fflush(stdout);
        return true;
    }
    /* "battery": sleep exactly as on battery (auto-sleep, Wi-Fi rest), but without forcing
     * Wi-Fi off like "nap": the way to test the real battery path over the USB cable. */
    if (!strcmp(line, "battery")) {
        dou_state_set_as_if_battery(true);
        set_asleep(true, "serial battery");
        return true;
    }
    if (!strcmp(line, "battery.off")) {
        dou_state_set_as_if_battery(false);
        return true;
    }
    if (!strcmp(line, "nap")) {
        dou_state_set_as_if_battery(true);
        set_asleep(true, "serial");
        s_nap_now = true;
        return true;
    }
#if LV_USE_SNAPSHOT
#if CONFIG_DOU_LATEST_REPLY
    if (dou_latest_reply_bench(line)) {
        set_asleep(false, "reply review");
        return true;
    }
#endif
    if (!strcmp(line, "ui.demo=1") || !strcmp(line, "ui.demo=0")) {
        dou_ui_bench_demo(!strcmp(line, "ui.demo=1"));
        return true;
    }
    if (!strcmp(line, "ui=settings") || !strcmp(line, "ui=face")) {
        dou_ui_bench_page(!strcmp(line, "ui=settings"));
        return true;
    }
    if (!strncmp(line, "ui.settings=", 12)) {
        if (dou_settings_ui_bench_open(line + 12)) {
            dou_ui_bench_page(true);
        } else {
            printf("@ui.error unknown settings page \"%s\": home wifi dou ble sound sleep battery power\n", line + 12);
            fflush(stdout);
        }
        return true;
    }
#if CONFIG_DOU_AVATAR_SYNC && CONFIG_DOU_UI_THEME
    if (!strncmp(line,"ui.settings.action=",19)) {
        if (dou_settings_ui_bench_action(line+19)) dou_ui_bench_page(true);
        else {printf("@ui.error settings action must be primary or back, with no action pending\n");fflush(stdout);}
        return true;
    }
#endif
#endif
#if CONFIG_DOU_WAKEWORD_ENABLE
    if (!strcmp(line, "wake") || !strcmp(line, "wake.on") || !strcmp(line, "wake.off")) {
        if (line[4] == '.') {
            dou_wakeword_set_enabled(!strcmp(line, "wake.on"));
        }
        dou_wakeword_status_t st;
        dou_wakeword_status(&st);
        printf("@wake {\"enabled\":%s,\"listening\":%s,\"runner\":\"%s\",\"wakes\":%u,\"cancels\":%u,"
               "\"submits\":%u,\"drops\":%u}\n",
               st.enabled ? "true" : "false", st.listening ? "true" : "false", st.runner,
               (unsigned)st.wakes, (unsigned)st.cancels, (unsigned)st.submits,
               (unsigned)st.dropped_frames);
        fflush(stdout);
        return true;
    }
#endif
#if CONFIG_DOU_UI_THEME
    if (!strcmp(line,"theme") || !strncmp(line,"theme=",6)) {
        if(line[5]=='=') {
            const char *name=line+6;
            if(!strcmp(name,"auto"))dou_theme_request_mode(DOU_THEME_AUTO);
            else if(!strcmp(name,"dark"))dou_theme_request_mode(DOU_THEME_DARK);
            else if(!strcmp(name,"light"))dou_theme_request_mode(DOU_THEME_LIGHT);
            else {printf("@theme error invalid\n");return true;}
        }
        static const char *const names[]={"auto","dark","light"};
        uint32_t hint=dou_theme_hint();
        printf("@theme {\"mode\":\"%s\",\"hint_valid\":%s,\"hint_rgb\":%u,\"save_error\":%d}\n",
            names[dou_theme_mode()],(hint&0x1000000u)?"true":"false",(unsigned)(hint&0xffffff),dou_theme_save_error());
        return true;
    }
#endif
    if (!strncmp(line, "face=", 5)) {
        set_face(line + 5);
        return true;
    }
    if (strncmp(line, "chat", 4) != 0) {
        return false;
    }
#if CONFIG_DOU_HATCH
    if (!strcmp(line, "chat.cancel")) {
        chat_cancel();
        return true;
    }
    bool last = !strncmp(line, "chat=", 5);
    if (last || !strncmp(line, "chat+=", 6)) {
        chat_line(line + (last ? 5 : 6), last, whole);
        return true;
    }
    return false;
#else
    (void)whole;
    dou_hatch_console("error", "THIS BOARD CAN'T CHAT OVER SERIAL", NULL);
    return true;
#endif
}

/*
 * Bench testing over the USB cable: 'd' / 'u' act as the talk button going
 * down / up, so the voice path can be driven without a finger on the button;
 * 'm' plays a built-in MP3 through the reply decoder; 'a' / 's' press the
 * menu's Down / Select; 'p' sends a screenshot; 'z' / 'w' sleep and wake.
 * A line starting with '>' is a setup command, the same "key=value" text as
 * the BLE CMD characteristic, or one of the console's own: "status" prints
 * the device's state, "power" the battery meter (dou_battery.h) and
 * "power.reset" starts it over, "nap" sleeps and leaves Wi-Fi at once (as
 * two minutes asleep on battery would; 'w' rejoins), "face=" shows a face
 * (see set_face), "wake" prints the wake word state, "wake.on" and "wake.off"
 * toggle listening, and "chat=" sends a typed message to Dou (see chat_line
 * and tools/dou/chat.py).
 */
static void serial_task(void *arg)
{
    if (dou_console_install(SERIAL_RX) != ESP_OK) {
        vTaskDelete(NULL);
    }
    for (;;) {
        uint8_t c;
        if (!dou_console_getc(&c)) {
            continue;
        }
        if (c == 'm') {
            dou_voice_request_mp3test();
        } else if (c == 'a' || c == 's') {
            dou_state_poke();
            dou_menu_key(c == 'a' ? DOU_MENU_DOWN : DOU_MENU_SELECT);
        } else if (c == 'p') {
            dou_ui_request_snapshot();
        } else if (c == 'z' || c == 'w') {
            set_asleep(c == 'z', "serial");
        } else if (c == '>') {
            char *line = heap_caps_malloc(SERIAL_LINE, DOU_BIG_CAPS);
            char none[1];   /* no room: the line is read and dropped */
            bool whole = read_line(line ? line : none, line ? SERIAL_LINE : sizeof(none));
            if (line && !console_command(line, whole)) {
                dou_ble_command(line);
            }
            if (line) { volatile char *p = line; for (int i = 0; i < SERIAL_LINE; i++) p[i] = 0; }
            free(line);
        } else if (c == 'd' || c == 'u') {
            dou_state_poke();
            post(c == 'd' ? DOU_PTT_DOWN : DOU_PTT_UP, false);
        }
    }
}

void dou_input_set_console_hook(bool (*hook)(const char *line))
{
    s_console_hook = hook;
}

esp_err_t dou_input_start(QueueHandle_t queue)
{
    s_queue = queue;
    if (xTaskCreate(input_task, "dou_input", 4096, NULL, 6, &s_input) != pdPASS) {
        return ESP_FAIL;
    }
    /* Bench-test and setup console; the input still works if it can't start. */
    xTaskCreate(serial_task, "dou_serial", 3584, NULL, 5, NULL);
    return ESP_OK;
}

void dou_input_request_power_off(void)
{
    s_power_off_requested = true;
}

void dou_input_request_restart(void)
{
    s_restart_requested = true;
}
