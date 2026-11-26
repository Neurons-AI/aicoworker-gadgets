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
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

/*
 * StopWatch with realtime enabled defaults to a held-mic call: hold yellow to
 * talk, release to listen in the same call, blue to end. Release hides the mic
 * meter immediately and keeps at most 120 ms of capture tail for codec lag.
 * Settings > Sound can select voice notes instead. Other boards retain their
 * existing note/tap flow.
 * Notes recorded out of reach are saved to PSRAM on boards with PSRAM.
 * Consumes dou_input_event_t from `queue` and drives dou_state for the UI.
 */
esp_err_t dou_voice_start(QueueHandle_t queue);

/* UI-loop state, including startup and teardown. PTT is pinned for the call. */
typedef enum {
    DOU_VOICE_VIEW_NONE,
    DOU_VOICE_VIEW_CONNECTING,
    DOU_VOICE_VIEW_READY,
    DOU_VOICE_VIEW_LISTENING,
    DOU_VOICE_VIEW_THINKING,
    DOU_VOICE_VIEW_WORKING,   /* nothing playing or said, Dou still has a caller turn open */
    DOU_VOICE_VIEW_SPEAKING,
    DOU_VOICE_VIEW_ENDING,
} dou_voice_view_t;
dou_voice_view_t dou_voice_call_view(void);
bool dou_voice_in_call(void);
bool dou_voice_ptt_call(void);
bool dou_voice_ptt_enabled(void);   /* next press, not the current call */
const char *dou_voice_call_label(void);   /* NULL outside a PTT call */
const char *dou_voice_call_hint(void);    /* yellow button, NULL outside PTT */
/* Bounded best effort before software poweroff; suppresses new calls until
 * shutdown is cancelled (only when the board's power-off operation fails). */
void dou_voice_shutdown(int wait_ms);
void dou_voice_cancel_shutdown(void);

#if CONFIG_DOU_WAKEWORD_ENABLE
/* Wake word: the next record() starts with this audio (copied,
 * whole 20 ms chunks, at most a second) instead of the push-to-talk
 * pre-roll ring, so a hands-free note includes the wake word itself. */
void dou_voice_seed_preroll(const int16_t *pcm, size_t frames);   /* NULL clears */
#endif

/* While on (settings' Sound page), idle mic audio feeds dou_voice_monitor_db(). */
void dou_voice_set_monitor(bool on);
/* Smoothed mic level in dBFS (fast attack, slow release). */
float dou_voice_monitor_db(void);

/* Plays a short chirp at the current volume (when idle). */
void dou_voice_request_chirp(void);

/* Runs dou_audio_loopback_test() at the current volume (when idle); results go to the log. */
void dou_voice_request_loopback(void);

/* Bench test: decodes and plays a built-in MP3 reply. */
void dou_voice_request_mp3test(void);

/* Asleep with nothing to play: codecs off, Wi-Fi dozing. */
bool dou_voice_resting(void);

/* Reply audio has drained: use a quiet avatar pose, retaining reply status,
 * captions and progress. False again as soon as more audio plays. */
bool dou_voice_reply_waiting(void);

/* Voice notes recorded out of Dou's reach wait to go, the oldest from the
 * last half hour: worth keeping Wi-Fi up for. */
bool dou_voice_notes_waiting(void);
