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
 * Firmware glue for hands-free wake word ("Hey Dou"): the audio
 * tap in dou_voice's read loop, the wake task that feeds the runner
 * and ticks the session core, synthetic-PTT posts to the dou_input
 * queue, and the runtime opt-in. The pure
 * logic lives in the dou_wakeword component (dou_wakeword.h); this
 * glue lives here, beside dou_voice/dou_state, because the voice
 * loop calls *into* it while it needs dou_voice's seeding and
 * dou_state's captions, and components cannot require each other.
 *
 * Every entry point is safe to call with the feature compiled out
 * (CONFIG_DOU_WAKEWORD_ENABLE=n): the inline stubs at the end do
 * nothing and report the runner as "off", so call sites need no
 * conditionals and builds without the feature compile no code for it.
 */

typedef struct {
    bool enabled;
    bool listening;
    const char *runner;
    int state;   /* dou_ww_state_t, kept as int to stay dependency-free */
    uint32_t wakes, cancels, submits, conversions, suppressed_wakes;
    uint32_t dropped_frames;   /* ring overload evidence */
} dou_wakeword_status_t;

#if CONFIG_DOU_WAKEWORD_ENABLE

/* Once, after dou_voice_start. Synthetic events go to `queue` (the
 * dou_input queue). Failure only loses wake word; voice still runs. */
esp_err_t dou_wakeword_init(QueueHandle_t queue);

/* Tap hook: one chunk just read by dou_voice. Cheap and
 * non-blocking (a ring copy); inference never runs here. */
void dou_wakeword_on_audio(const int16_t *pcm, size_t frames);

/* A physical talk-button press (down=true) or release seen by
 * dou_voice, for the preemption rules. Synthetic events are
 * never reported through here. */
void dou_wakeword_note_ptt(bool down);

/* Runtime opt-in: persists via dou_settings. The setters in this
 * header only publish flags; the wake task applies them at its next
 * tick (disabling closes any hands-free capture in progress). */
void dou_wakeword_set_enabled(bool on);
bool dou_wakeword_enabled(void);
/* Whether a hands-free turn whose press carries `disables` may still go:
 * false once listening was turned off after its wake was heard. */
bool dou_wakeword_turn_wanted(uint32_t disables);
/* Enabled, not suppressed by playback, and not resting: what the
 * listening indicator shows. Resting follows dou_wakeword_enabled(). */
bool dou_wakeword_listening(void);

/* Reply audio (or a chirp/test) is playing: wakes are discarded and
 * pre-roll is cleared, so Dou never triggers on its own voice. */
void dou_wakeword_set_playback(bool on);

/* A physical push-to-talk turn is recording: the runner is paused
 * so the speech being recorded cannot wake a second turn.
 * Ending it clears pre-roll and resets the runner, like
 * dou_voice's own pre_reset() after a turn. */
void dou_wakeword_set_ptt_turn(bool active);

/* Voice task only, once per hands-free turn, before anything of it goes:
 * the chunks it had already tapped past the seed's end when the wake task
 * seeded the turn (the wake task can trail by up to a tap's worth), oldest
 * first, so the note continues without a gap. Pointers into the glue's
 * copy, valid until the next tap; *count says how many (0 once taken).
 * Returns false if some of them were already overwritten (sustained
 * overload): the note can't be continuous, so the turn must end unsent. */
#define DOU_WAKEWORD_BACKLOG_CHUNKS 32   /* the 25-chunk tap plus the few read before record(); a power of two */
bool dou_wakeword_take_backlog(const int16_t *chunks[], size_t max, size_t *count);

/* dou_voice ended a hands-free turn by itself (its 15 s cap, a failure,
 * or a turn that never started or didn't qualify), not at the glue's own
 * release: the glue closes its capture so the next wake is heard. */
void dou_wakeword_turn_ended(void);

/* "executorch", or "off" (not compiled in): the runner in logs/status. */
const char *dou_wakeword_runner_kind(void);

void dou_wakeword_status(dou_wakeword_status_t *out);

#else   /* !CONFIG_DOU_WAKEWORD_ENABLE */

static inline esp_err_t dou_wakeword_init(QueueHandle_t queue)
{
    (void)queue;
    return ESP_OK;
}
static inline void dou_wakeword_on_audio(const int16_t *pcm, size_t frames)
{
    (void)pcm;
    (void)frames;
}
static inline void dou_wakeword_note_ptt(bool down) { (void)down; }
static inline void dou_wakeword_set_enabled(bool on) { (void)on; }
static inline bool dou_wakeword_enabled(void) { return false; }
static inline bool dou_wakeword_turn_wanted(uint32_t disables) { (void)disables; return false; }
static inline bool dou_wakeword_listening(void) { return false; }
static inline void dou_wakeword_set_playback(bool on) { (void)on; }
static inline void dou_wakeword_set_ptt_turn(bool active) { (void)active; }
static inline void dou_wakeword_turn_ended(void) {}
static inline const char *dou_wakeword_runner_kind(void) { return "off"; }
static inline void dou_wakeword_status(dou_wakeword_status_t *out)
{
    if (out) {
        *out = (dou_wakeword_status_t){ .runner = "off" };
    }
}

#endif
