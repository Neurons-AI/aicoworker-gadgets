#define _POSIX_C_SOURCE 200809L

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

#include <SDL.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"

#include "dou_state.h"
#include "dou_ui.h"
#include "dou_options.h"
#include "sim_board.h"
#include "sim_platform.h"
#include "sim_services.h"

#define FRAME_STEP_MS 5

static volatile sig_atomic_t s_quit;
static bool s_ready;
static float s_level;
static float s_progress;
static dou_power_t s_power = {
    .battery_pct = 72,
    .battery_mv = 3970,
    .charging = false,
    .usb = true,
};
static dou_ble_state_t s_ble_state = DOU_BLE_CONNECTED;
static uint32_t s_passkey;
static char s_ble_name[32] = "DouGadget-SIM001";

static void usage(FILE *out, const char *argv0)
{
    fprintf(out,
            "Usage: %s [--headless] [--scenario FILE] [--run-ms N] "
            "[--screenshot FILE.ppm]\n"
            "\n"
            "Scenario lines are key=value. Supported keys:\n"
            "  face=boot|idle|listening|thinking|speaking|error|off|happy\n"
            "  caption=TEXT       level=0..1        progress=0..1\n"
            "  battery=-1..100    battery_mv=MV     usb=true|false\n"
            "  charging=true|false                  asleep=true|false\n"
            "  wifi=off|no_network|connecting|connected|failed|not_nearby\n"
            "  ble=off|advertising|connected         passkey=0..999999\n"
            "  paired=true|false  link=boot|unpaired|pairing|confirm|connecting|online|offline|error\n"
            "  speaker=true|false call=true|false brightness=10..100\n"
            "  image=RRGGBB|hide  advance=MILLISECONDS\n"
            "\n"
            "Interactive keys: F1..F7 select face states, H is happy, Space is\n"
            "push-to-talk, +/- change level, [/] change progress, S sleeps,\n"
            "P writes dou-simulator.ppm, Esc quits. Mouse input is touch.\n",
            argv0);
}

static void on_signal(int signum)
{
    (void)signum;
    s_quit = 1;
}

static void render_for(uint32_t duration_ms, bool real_time)
{
    uint32_t elapsed = 0;
    while (!s_quit && elapsed < duration_ms) {
        uint32_t step = duration_ms - elapsed;
        if (step > FRAME_STEP_MS) {
            step = FRAME_STEP_MS;
        }
        sim_time_advance_us((int64_t)step * 1000);
        lv_timer_handler();
        if (real_time) {
            SDL_Delay(step);
        }
        elapsed += step;
    }
}

static bool write_snapshot(const char *path)
{
    lv_display_t *display = sim_board_display();
    if (!display) {
        fprintf(stderr, "simulator display is not ready\n");
        return false;
    }
    lv_refr_now(display);
    SDL_Renderer *renderer = lv_sdl_window_get_renderer(display);
    int width = 0;
    int height = 0;
    if (!renderer || SDL_GetRendererOutputSize(renderer, &width, &height) != 0
        || width <= 0 || height <= 0) {
        fprintf(stderr, "could not read simulator renderer: %s\n", SDL_GetError());
        return false;
    }
    size_t row_bytes = (size_t)width * 3;
    uint8_t *pixels = malloc(row_bytes * (size_t)height);
    if (!pixels) {
        fprintf(stderr, "could not allocate simulator screenshot\n");
        return false;
    }
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB24, pixels,
                             (int)row_bytes) != 0) {
        fprintf(stderr, "could not capture simulator screen: %s\n", SDL_GetError());
        free(pixels);
        return false;
    }
    FILE *out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        free(pixels);
        return false;
    }
    fprintf(out, "P6\n%d %d\n255\n", width, height);
    if (fwrite(pixels, row_bytes, (size_t)height, out) != (size_t)height) {
        fprintf(stderr, "%s: write failed\n", path);
        fclose(out);
        free(pixels);
        return false;
    }
    bool ok = fclose(out) == 0;
    free(pixels);
    if (ok) {
        fprintf(stderr, "%s: %dx%d\n", path, width, height);
    }
    return ok;
}

static bool parse_bool(const char *text, bool *out)
{
    if (!strcmp(text, "true") || !strcmp(text, "on") || !strcmp(text, "1")) {
        *out = true;
        return true;
    }
    if (!strcmp(text, "false") || !strcmp(text, "off") || !strcmp(text, "0")) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_long(const char *text, long min, long max, long *out)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < min || value > max) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_float(const char *text, float min, float max, float *out)
{
    char *end = NULL;
    errno = 0;
    float value = strtof(text, &end);
    if (errno || end == text || *end || !isfinite(value) || value < min || value > max) {
        return false;
    }
    *out = value;
    return true;
}

static void select_mode(dou_mode_t mode)
{
    /* Production shutdown permits only Idle next. Preview controls should
     * still be able to select any state after showing Off. */
    if (dou_state_mode(NULL) == DOU_MODE_OFF && mode != DOU_MODE_OFF) {
        dou_state_set_mode(DOU_MODE_IDLE);
    }
    dou_state_set_mode(mode);
}

static bool set_face(const char *value)
{
    static const struct {
        const char *name;
        dou_mode_t mode;
    } modes[] = {
        { "boot", DOU_MODE_BOOT },
        { "idle", DOU_MODE_IDLE },
        { "listening", DOU_MODE_LISTENING },
        { "thinking", DOU_MODE_THINKING },
        { "speaking", DOU_MODE_SPEAKING },
        { "error", DOU_MODE_ERROR },
        { "off", DOU_MODE_OFF },
    };
    if (!strcmp(value, "happy")) {
        select_mode(DOU_MODE_IDLE);
        dou_state_make_happy();
        return true;
    }
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (!strcmp(value, modes[i].name)) {
            select_mode(modes[i].mode);
            return true;
        }
    }
    return false;
}

static bool set_wifi(const char *value)
{
    static const struct {
        const char *name;
        dou_wifi_state_t state;
    } states[] = {
        { "off", DOU_WIFI_OFF },
        { "no_network", DOU_WIFI_NO_NETWORK },
        { "connecting", DOU_WIFI_CONNECTING },
        { "connected", DOU_WIFI_CONNECTED },
        { "failed", DOU_WIFI_FAILED },
        { "not_nearby", DOU_WIFI_NOT_NEARBY },
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        if (!strcmp(value, states[i].name)) {
            sim_services_set_wifi(states[i].state,
                                  states[i].state == DOU_WIFI_CONNECTED ? "Dou Simulator" : "");
            return true;
        }
    }
    return false;
}

static bool set_ble(const char *value)
{
    static const struct {
        const char *name;
        dou_ble_state_t state;
    } states[] = {
        { "off", DOU_BLE_OFF },
        { "advertising", DOU_BLE_ADVERTISING },
        { "connected", DOU_BLE_CONNECTED },
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        if (!strcmp(value, states[i].name)) {
            s_ble_state = states[i].state;
            sim_services_set_ble(s_ble_state, s_ble_name, s_passkey);
            return true;
        }
    }
    return false;
}

static bool set_link(const char *value)
{
    static const struct {
        const char *name;
        dou_link_state_t state;
    } states[] = {
        { "boot", DOU_LINK_BOOT },
        { "unpaired", DOU_LINK_UNPAIRED },
        { "pairing", DOU_LINK_PAIRING },
        { "confirm", DOU_LINK_CONFIRM },
        { "connecting", DOU_LINK_CONNECTING },
        { "online", DOU_LINK_ONLINE },
        { "offline", DOU_LINK_OFFLINE },
        { "error", DOU_LINK_ERROR },
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        if (!strcmp(value, states[i].name)) {
            sim_services_set_link_state(states[i].state);
            return true;
        }
    }
    return false;
}

static bool draw_test_image(const char *value)
{
    if (!strcmp(value, "hide")) {
        dou_ui_image_hide();
        return true;
    }
    if (strlen(value) != 6) {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long rgb = strtoul(value, &end, 16);
    if (errno || end != value + 6) {
        return false;
    }
    int w = 0, h = 0;
    if (!dou_ui_image_size(&w, &h) || w <= 0 || h <= 0) {
        return false;
    }
    unsigned r = (rgb >> 16) & 0xff;
    unsigned g = (rgb >> 8) & 0xff;
    unsigned b = rgb & 0xff;
    uint8_t *row = malloc((size_t)w * 2);
    if (!row) {
        return false;
    }
    bool ok = true;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned shade = (unsigned)((x + y) & 0x1f);
            unsigned rx = r + shade > 255 ? 255 : r + shade;
            unsigned gx = g + shade > 255 ? 255 : g + shade;
            uint16_t px565 = (uint16_t)(((rx >> 3) << 11) | ((gx >> 2) << 5) | (b >> 3));
            row[2 * x] = (uint8_t)(px565 >> 8);
            row[2 * x + 1] = (uint8_t)(px565 & 0xff);
        }
        if (!dou_ui_image_draw(0, y, w, 1, (const uint16_t *)row)) {
            ok = false;
            break;
        }
    }
    free(row);
    if (ok) {
        dou_ui_image_done();
    }
    return ok;
}

static bool apply_setting(const char *key, const char *value, bool real_time)
{
    bool flag;
    long number;
    float scalar;
    if (!strcmp(key, "face")) {
        return set_face(value);
    }
    if (!strcmp(key, "caption")) {
        dou_state_set_caption("%s", value);
        return true;
    }
    if (!strcmp(key, "options")) {
        dou_options_t q = { .selected = -1 };
        snprintf(q.widget_id, sizeof(q.widget_id), "sim-options");
        snprintf(q.message_id, sizeof(q.message_id), "sim-message");
        snprintf(q.question, sizeof(q.question), "Which lunch would you like?");
        const char *p = value;
        while (*p && q.count < DOU_OPTIONS_MAX) {
            const char *end = strchr(p, '|');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (!len || len >= DOU_OPTION_BYTES) return false;
            memcpy(q.labels[q.count++], p, len);
            if (!end) return dou_options_publish(&q);
            p = end + 1;
        }
        return false;
    }
    if (!strcmp(key, "level") && parse_float(value, 0.0f, 1.0f, &scalar)) {
        s_level = scalar;
        dou_state_set_level(s_level);
        return true;
    }
    if (!strcmp(key, "progress") && parse_float(value, 0.0f, 1.0f, &scalar)) {
        s_progress = scalar;
        dou_state_set_progress(s_progress);
        return true;
    }
    if (!strcmp(key, "battery") && parse_long(value, -1, 100, &number)) {
        s_power.battery_pct = (int)number;
        dou_state_set_power(&s_power);
        return true;
    }
    if (!strcmp(key, "battery_mv") && parse_long(value, 0, 6000, &number)) {
        s_power.battery_mv = (int)number;
        dou_state_set_power(&s_power);
        return true;
    }
    if (!strcmp(key, "usb") && parse_bool(value, &flag)) {
        s_power.usb = flag;
        dou_state_set_power(&s_power);
        return true;
    }
    if (!strcmp(key, "charging") && parse_bool(value, &flag)) {
        s_power.charging = flag;
        dou_state_set_power(&s_power);
        return true;
    }
    if (!strcmp(key, "asleep") && parse_bool(value, &flag)) {
        dou_state_set_asleep(flag);
        return true;
    }
    if (!strcmp(key, "wifi")) {
        return set_wifi(value);
    }
    if (!strcmp(key, "ble")) {
        return set_ble(value);
    }
    if (!strcmp(key, "passkey") && parse_long(value, 0, 999999, &number)) {
        s_passkey = (uint32_t)number;
        sim_services_set_ble(s_ble_state, s_ble_name, s_passkey);
        return true;
    }
    if (!strcmp(key, "paired") && parse_bool(value, &flag)) {
        sim_services_set_paired(flag);
        return true;
    }
    if (!strcmp(key, "link")) {
        return set_link(value);
    }
    if (!strcmp(key, "call") && parse_bool(value, &flag)) {
        sim_services_set_call(flag);
        return true;
    }
    if (!strcmp(key, "speaker") && parse_bool(value, &flag)) {
        sim_services_set_speaker(flag);
        return true;
    }
    if (!strcmp(key, "brightness") && parse_long(value, 10, 100, &number)) {
        sim_services_set_brightness((int)number);
        return true;
    }
    if (!strcmp(key, "image")) {
        return draw_test_image(value);
    }
    if (!strcmp(key, "advance") && parse_long(value, 0, 3600000, &number)) {
        render_for((uint32_t)number, real_time);
        return true;
    }
    return false;
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }
    size_t n = strlen(text);
    while (n && isspace((unsigned char)text[n - 1])) {
        text[--n] = '\0';
    }
    return text;
}

static bool run_scenario(const char *path, bool real_time)
{
    FILE *in = fopen(path, "r");
    if (!in) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return false;
    }
    char line[1024];
    unsigned line_no = 0;
    bool ok = true;
    while (ok && fgets(line, sizeof(line), in)) {
        line_no++;
        char *entry = trim(line);
        if (!*entry || *entry == '#') {
            continue;
        }
        char *equals = strchr(entry, '=');
        if (!equals) {
            fprintf(stderr, "%s:%u: expected key=value\n", path, line_no);
            ok = false;
            break;
        }
        *equals = '\0';
        char *key = trim(entry);
        char *value = trim(equals + 1);
        if (!apply_setting(key, value, real_time)) {
            fprintf(stderr, "%s:%u: unsupported or invalid setting: %s=%s\n",
                    path, line_no, key, value);
            ok = false;
        }
    }
    if (ferror(in)) {
        fprintf(stderr, "%s: read failed\n", path);
        ok = false;
    }
    fclose(in);
    return ok;
}

static int event_watch(void *userdata, SDL_Event *event)
{
    (void)userdata;
    if (event->type == SDL_QUIT) {
        s_quit = 1;
        return 1;
    }
    if (!s_ready || (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP)) {
        return 1;
    }
    bool down = event->type == SDL_KEYDOWN;
    if (down && event->key.repeat) {
        return 1;
    }
    SDL_Keycode key = event->key.keysym.sym;
    if (down && key == SDLK_ESCAPE) {
        s_quit = 1;
    } else if (down && key >= SDLK_F1 && key <= SDLK_F7) {
        static const dou_mode_t modes[] = {
            DOU_MODE_BOOT, DOU_MODE_IDLE, DOU_MODE_LISTENING, DOU_MODE_THINKING,
            DOU_MODE_SPEAKING, DOU_MODE_ERROR, DOU_MODE_OFF,
        };
        select_mode(modes[key - SDLK_F1]);
    } else if (down && key == SDLK_h) {
        select_mode(DOU_MODE_IDLE);
        dou_state_make_happy();
    } else if (key == SDLK_SPACE) {
        select_mode(down ? DOU_MODE_LISTENING : DOU_MODE_THINKING);
    } else if (down && (key == SDLK_PLUS || key == SDLK_EQUALS || key == SDLK_KP_PLUS)) {
        s_level = s_level < 0.9f ? s_level + 0.1f : 1.0f;
        dou_state_set_level(s_level);
    } else if (down && (key == SDLK_MINUS || key == SDLK_KP_MINUS)) {
        s_level = s_level > 0.1f ? s_level - 0.1f : 0.0f;
        dou_state_set_level(s_level);
    } else if (down && key == SDLK_RIGHTBRACKET) {
        s_progress = s_progress < 0.9f ? s_progress + 0.1f : 1.0f;
        dou_state_set_progress(s_progress);
    } else if (down && key == SDLK_LEFTBRACKET) {
        s_progress = s_progress > 0.1f ? s_progress - 0.1f : 0.0f;
        dou_state_set_progress(s_progress);
    } else if (down && key == SDLK_s) {
        dou_state_set_asleep(!dou_state_asleep());
    } else if (down && key == SDLK_p) {
        (void)write_snapshot("dou-simulator.ppm");
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *scenario = NULL;
    const char *screenshot = NULL;
    uint32_t run_ms = 1000;
    bool headless = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) {
            headless = true;
        } else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) {
            scenario = argv[++i];
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (!strcmp(argv[i], "--run-ms") && i + 1 < argc) {
            long value;
            if (!parse_long(argv[++i], 0, 3600000, &value)) {
                fprintf(stderr, "invalid --run-ms value\n");
                return 2;
            }
            run_ms = (uint32_t)value;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout, argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            usage(stderr, argv[0]);
            return 2;
        }
    }

    if (headless) {
        (void)setenv("SDL_VIDEODRIVER", "dummy", 1);
        (void)setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    sim_time_reset();
    sim_services_reset();
    lv_init();
    dou_state_init();
    dou_state_set_power(&s_power);
    dou_board = sim_board_get();
    int pre_w = 0, pre_h = 0;
    if (!dou_ui_image_size(&pre_w, &pre_h)
        || pre_w != dou_board->width || pre_h != dou_board->height) {
        fprintf(stderr, "dou_ui_image_size failed before dou_ui_start\n");
        return 1;
    }
    if (dou_board->init && dou_board->init() != ESP_OK) {
        fprintf(stderr, "simulator board initialization failed\n");
        return 1;
    }
    if (dou_ui_start() != ESP_OK) {
        fprintf(stderr, "UI initialization failed\n");
        return 1;
    }
    SDL_AddEventWatch(event_watch, NULL);
    s_ready = true;

    if (scenario && !run_scenario(scenario, !headless)) {
        return 2;
    }
    if (headless || screenshot) {
        render_for(run_ms, false);
        if (screenshot && !write_snapshot(screenshot)) {
            return 1;
        }
    } else {
        while (!s_quit) {
            render_for(FRAME_STEP_MS, true);
        }
    }

    SDL_DelEventWatch(event_watch, NULL);
    lv_deinit();
    return 0;
}
