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

/*
 * Shared state between the voice pipeline (writer) and the UI (reader).
 * Scalars are plain word-sized stores; the caption is guarded by a spinlock.
 */

typedef enum {
    DOU_MODE_BOOT = 0,
    DOU_MODE_IDLE,
    DOU_MODE_LISTENING,
    DOU_MODE_THINKING,
    DOU_MODE_SPEAKING,
    DOU_MODE_ERROR,
    DOU_MODE_OFF,       /* powering down */
    DOU_MODE_COUNT,
} dou_mode_t;

typedef struct {
    int battery_pct;   /* -1 when no battery is connected */
    int battery_mv;    /* 0 when the board can't measure it */
    bool charging;
    bool usb;
} dou_power_t;

void dou_state_init(void);

void dou_state_set_mode(dou_mode_t mode);
dou_mode_t dou_state_mode(float *secs_in_mode);

/* Live audio level (mic while listening, playback while speaking), 0..1. */
void dou_state_set_level(float level);
float dou_state_level(void);

/* Progress of the current phase (recording length or playback), 0..1. */
void dou_state_set_progress(float progress);
float dou_state_progress(void);

/* Room for a page of reply text; longer captions are cut short. */
#define DOU_CAPTION_MAX 400

void dou_state_set_caption(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Copies the caption if it changed since *version; returns true on change. */
bool dou_state_caption(char *out, size_t out_len, uint32_t *version);

/* A transient hint caption: cleared once DOU_HINT_MS passes. Plain
 * captions (replies, status) never expire; a hint set replaces any
 * caption, and any later plain caption cancels the pending expiry. */
#define DOU_HINT_MS 5000
void dou_state_set_hint(const char *text);
/* Whether the caption is a hint that hasn't expired yet. */
bool dou_state_hint_pending(void);

/* How much reply text the screen shows at once, set by the UI: lines of up to
 * `cols` characters. The replies are wrapped and paged to fit. A screen that
 * draws CJK larger than its other caption text sets a page for replies with CJK
 * in them too, and one with its accented letters' font (dou_text_has_latin)
 * for replies with those; without one they get the usual page. */
void dou_state_set_page(int cols, int lines);
void dou_state_set_cjk_page(int cols, int lines);
void dou_state_set_latin_page(int cols, int lines);
/* The page for `text`. */
void dou_state_page(const char *text, int *cols, int *lines);

void dou_state_set_power(const dou_power_t *power);
dou_power_t dou_state_power(void);
/* A battery and no USB power (charger or computer). Power saving asleep (the
 * voice task resting, the display paused, the Wi-Fi nap) is only for then;
 * a change nudges (dou_state_wait_awake). */
bool dou_state_on_battery(void);
/* Bench tests over USB (">nap"): count as on battery until turned off. */
void dou_state_set_as_if_battery(bool on);

/* Any user interaction: keeps Dou awake. */
void dou_state_poke(void);
float dou_state_idle_secs(void);

/* Screen sleep (display dark, rendering paused). Waking also pokes. */
void dou_state_set_asleep(bool asleep);
bool dou_state_asleep(void);
/* For a task with nothing to do asleep: returns once awake, nudged
 * (dou_state_nudge, which doesn't wake the screen) or timeout_ms passes. */
void dou_state_wait_awake(uint32_t timeout_ms);
void dou_state_nudge(void);

/* A reply from Dou arrived: wakes the screen and flashes it twice
 * (dou_state_flash_level, read by the UI each frame). */
void dou_state_flash(void);
float dou_state_flash_level(void);   /* 0..1, 0 when no flash is running */

/* Touch/pet reaction. */
void dou_state_make_happy(void);
float dou_state_happiness(void);
