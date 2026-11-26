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

#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "sdkconfig.h"
#if CONFIG_DOU_UI_FREEINK
/* FreeInk boards build without LVGL; its types stay opaque here. */
typedef struct _lv_display_t lv_display_t;
typedef struct _lv_indev_t lv_indev_t;
typedef uint8_t lv_align_t;
#else
#include "lvgl.h"
#endif

#include "dou_state.h"
#if CONFIG_DOU_MOTION_GESTURES
#include "dou_motion_core.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What Dou needs from the hardware. Each file in boards/ fills one of these in
 * and returns it from dou_board_get(). Everything above this line (UI, voice
 * loop, settings, Dou chat) is shared; Wi-Fi and BLE belong to Home Link.
 */

/* Button edges reported by poll_buttons(). */
#define DOU_BTN_TALK_PRESS   (1u << 0)
#define DOU_BTN_TALK_RELEASE (1u << 1)
#define DOU_BTN_AUX_PRESS    (1u << 2)
#define DOU_BTN_AUX_RELEASE  (1u << 3)
/* Keyboard navigation presses, independent of the two-button controls. */
#define DOU_BTN_UP           (1u << 4)
#define DOU_BTN_DOWN         (1u << 5)
#define DOU_BTN_LEFT         (1u << 6)
#define DOU_BTN_RIGHT        (1u << 7)
#define DOU_BTN_ENTER        (1u << 8)
#define DOU_BTN_ESCAPE       (1u << 9)
/* Dedicated controls; legacy two-button boards keep AUX's existing role.
 * POWER_LONG is a validated hold/PMIC long event, never a fabricated level. */
#define DOU_BTN_POWER_PRESS  (1u << 10)
#define DOU_BTN_POWER_RELEASE (1u << 11)
#define DOU_BTN_POWER_LONG   (1u << 12)
#define DOU_BTN_SPEAKER_PRESS (1u << 13)
#define DOU_BTN_SPEAKER_RELEASE (1u << 14)
/* Rotary reply navigation; these are steps, not held buttons. */
#if CONFIG_DOU_BOARD_SENSECAP_WATCHER
#define DOU_BTN_REVIEW_PREV  (1u << 15)
#define DOU_BTN_REVIEW_NEXT  (1u << 16)
#endif
/* With TALK_PRESS: reported once a hold passes its threshold (a finger held on
 * the face to talk), not at the press's own edge, so it never confirms pairing. */
#define DOU_BTN_TALK_HELD    (1u << 17)

/* Where a button's icon goes on screen: beside the button, inside the panel. */
typedef struct {
    lv_align_t align;
    int16_t x, y;
} dou_button_hint_t;

typedef struct {
    const char *name;
    int width, height;
    bool round;             /* circular panel: keep content inside the circle */
    bool epaper;            /* slow e-paper refresh: settings poll less often and skip live text */
    bool no_ring;           /* square panel: progress as a bar along the bottom edge, no ring around Dou */
    bool touch;             /* no touch: no settings screen, set up over BLE */
    float diagonal_in;      /* screen size; under 2" typing uses a keypad with bigger keys */
    bool touch_talk;        /* on-screen mic supplies talk edges, alone or beside a talk button */
    bool keyboard;          /* dedicated menu navigation keys */
    const char *talk_button;    /* where the buttons are, for captions: "top" */
    const char *aux_button;     /* "bottom" */
    const char *wake_button;    /* wakes it from power off, if not aux_button: "K0" */
    dou_button_hint_t talk_hint;   /* mic icon; the menu's hints follow both */
    dou_button_hint_t aux_hint;    /* power or menu icon; left out, there's none */
    /* Optional dedicated POWER and speaker controls, independent of AUX. */
    const char *power_button;
    const char *speaker_button;
    dou_button_hint_t power_hint;
    dou_button_hint_t speaker_hint;
    /* 466 px round-panel layout: dedicated 26-36 px rim icons, safe header chord
     * and central read-reply column. Requires all three hint positions/names. */
    bool rim_controls;
    /* The menu turns a right-edge aux button's hint on end, which LVGL draws
     * through a ~14 KB layer; set this where that's too much RAM (the C3) and
     * the hint lies flat on the menu's bar, left of the talk button's. */
    bool flat_menu_hints;
    int frame_ms;           /* face animation period */
    int avatar_px;          /* optional avatar canvas size; 0 uses the UI default */

    /* Power rails, buses, expanders. Runs first. */
    esp_err_t (*init)(void);

    /* Starts the panel, LVGL and its task; *touch may be left NULL. */
    lv_display_t *(*display_start)(lv_indev_t **touch);
    bool (*display_lock)(int timeout_ms);   /* -1 waits forever */
    void (*display_unlock)(void);
    void (*set_brightness)(int pct);        /* 0 = off */
    void (*panel_sleep)(bool sleep);        /* screen off; NULL: backlight only */
    /* Screen off with nothing to draw: stops LVGL's task and tick, and touch,
     * so the chip can light-sleep. Buttons still wake it. NULL: LVGL keeps
     * running. */
    void (*display_pause)(bool pause);
    /* FreeInk boards (CONFIG_DOU_UI_FREEINK): the touch point while a
     * finger is down, in screen coordinates (width x height, as above).
     * FreeInk drives the panel, so these boards leave display_start and the
     * display hooks above NULL. */
    bool (*touch_read)(int16_t *x, int16_t *y);
    /*
     * Video (CONFIG_DOU_VIDEO): draws a rectangle (end exclusive) straight
     * to the panel, bypassing LVGL, in pieces of `rows` rows that
     * fill(dst, stride, row, rows, arg) writes in the panel's byte order
     * (dst 16-byte aligned). Returns once it's all gone. NULL: no video.
     */
    void (*draw_frame)(int x1, int y1, int x2, int y2, int rows,
                       void (*fill)(uint8_t *dst, int stride, int row, int rows, void *arg), void *arg);

    /* Speaker codec and mic codec, not yet opened. A mic without a codec (a
     * PDM mic) leaves *mic NULL and is read through mic_read below. */
    esp_err_t (*audio_init)(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic);
    /* A microphone without a codec or speaker (a PDM mic), in place of
     * audio_init: set up once, powered on and off with the audio
     * (dou_audio_power), read as mono at DOU_AUDIO_RATE.
     * A board with mic_init has no speaker: replies are read, not heard.
     * mic_read also serves a board whose audio_init returns a speaker but
     * no mic codec, which sets the mic up there (mic_init and mic_power
     * stay NULL). dou_audio applies the mic gain to what mic_read returns,
     * in both cases. */
    esp_err_t (*mic_init)(void);
    esp_err_t (*mic_power)(bool on);
    esp_err_t (*mic_read)(int16_t *mono, size_t frames);
    int mic_slot;           /* slot carrying the mic (0/1), or -1 to mix both */
    void (*set_mic_gain)(esp_codec_dev_handle_t mic, int db);   /* NULL: esp_codec_dev_set_in_gain */
    /* Audio power the board can switch, such as an amp's enable: on once
     * the codecs are open, off before they close to rest. NULL: none. */
    void (*audio_power)(bool on);
    /* The codecs' own supply, where the board switches it: on before they
     * open (the first time too, before audio_init), off once both have
     * closed to rest. Off, nothing reaches them: dou_audio skips the volume
     * and gain setters until they open again, and reapplies the settings
     * then. NULL: always powered. */
    void (*codec_power)(bool on);

    /* Called every 10 ms from the input task (50 ms while the display is
     * paused, unless wait_buttons is set); returns DOU_BTN_* edges. */
    unsigned (*poll_buttons)(void);
    /* Display paused: returns once a button changes (waking the chip from
     * light sleep), the task is notified or timeout_ms passes, so the
     * buttons needn't be polled. NULL: polled. */
    void (*wait_buttons)(int timeout_ms);
#if CONFIG_DOU_MOTION_GESTURES
    /* Owned by the input task; read returns ESP_ERR_NOT_FINISHED for no new sample. */
    esp_err_t (*motion_enable)(bool enable);
    esp_err_t (*motion_read)(dou_motion_sample_t *out);
#endif
    esp_err_t (*read_power)(dou_power_t *out);
    /* Turns the board off; returns only on failure. NULL: it can't from
     * software (a physical switch does), and Settings has no Power off. */
    esp_err_t (*power_off)(void);
} dou_board_t;

/* The running board, set by dou_app_run(). */
extern const dou_board_t *dou_board;

/* Starts everything and returns; Home Link's app_main() calls it. */
void dou_app_run(const dou_board_t *board);

/* The board selected by CONFIG_DOU_BOARD_* (boards/). */
const dou_board_t *dou_board_get(void);

/* Debounced GPIO button, for poll_buttons() implementations. */
typedef struct {
    gpio_num_t gpio;
    bool active_high;
    bool pressed;
    uint8_t stable;
} dou_gpio_button_t;

/* Active low, with the internal pull-up where the pad has one. */
esp_err_t dou_gpio_button_init(dou_gpio_button_t *b, gpio_num_t gpio);
/* Active high and driven both ways by the board, so no internal pull. */
esp_err_t dou_gpio_button_init_high(dou_gpio_button_t *b, gpio_num_t gpio);
/* Returns DOU_BTN_TALK_PRESS / DOU_BTN_TALK_RELEASE on an edge; shift as needed. */
unsigned dou_gpio_button_poll(dou_gpio_button_t *b);
/* For wait_buttons(): blocks until one of the n buttons leaves its debounced
 * state, the calling task is notified (xTaskNotifyGive) or timeout_ms passes. */
void dou_gpio_buttons_wait(dou_gpio_button_t *const *b, int n, int timeout_ms);

#ifdef __cplusplus
}
#endif
