# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Exercise wheel click/hold decisions and retained reply navigation."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DOU = ROOT / "components/dou"


class WatcherControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        out = Path(cls.tmp.name)
        (out / "freertos").mkdir()
        (out / "esp_err.h").write_text("typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 1\n")
        (out / "esp_heap_caps.h").write_text("#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define heap_caps_malloc(n, caps) (fail_alloc ? NULL : malloc(n))\nextern int fail_alloc;\n")
        (out / "freertos/FreeRTOS.h").write_text("#define portMAX_DELAY 0\n")
        (out / "freertos/semphr.h").write_text("typedef int SemaphoreHandle_t;\n#define xSemaphoreCreateMutex() 1\n#define xSemaphoreTake(lock, wait) ((void)0)\n#define xSemaphoreGive(lock) ((void)0)\n")
        source = (DOU / "dou_input.c").read_text()
        # The function contains conditional camera blocks; take through its final brace.
        start = source.index("static void watcher_buttons(")
        end = source.index("\n/* A pairing prompt", start)
        watcher = source[start:end].rsplit("\n#endif", 1)[0]
        # The real talk button too: whether a delayed wheel press can confirm pairing is decided there.
        talk_start = source.index("/* Talk button: push-to-talk")
        talk = source[talk_start:source.index("\n/* Keyboard menus", talk_start)]
        harness = out / "controls.c"
        harness.write_text(r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dou_wheel.h"
#include "dou_review.h"
#include "dou_chat_priv.h"
int fail_alloc;
static int cols = 10, lines = 2;
void dou_state_page(const char *text, int *c, int *l) { (void)text; *c = cols; *l = lines; }
static void click(void) {
    dou_wheel_t s = {0};
    assert(dou_wheel_update(&s, true, 100, false, false) == DOU_WHEEL_NONE);
    assert(dou_wheel_update(&s, true, 399, false, false) == DOU_WHEEL_NONE);
    assert(dou_wheel_update(&s, false, 399, false, false) == DOU_WHEEL_SLEEP);
    assert(!s.recording);
    assert(dou_wheel_update(&s, false, 400, true, false) == DOU_WHEEL_NONE);
    assert(dou_wheel_update(&s, true, 500, true, false) == DOU_WHEEL_WAKE);
    assert(dou_wheel_update(&s, false, 550, false, false) == DOU_WHEEL_NONE);
    assert(dou_wheel_update(&s, true, 600, false, false) == DOU_WHEEL_NONE);
    assert(dou_wheel_update(&s, true, 900, false, false) == DOU_WHEEL_TALK_DOWN);
    assert(dou_wheel_update(&s, true, 1200, false, false) == DOU_WHEEL_NONE);
    assert(dou_wheel_update(&s, false, 1300, false, false) == DOU_WHEEL_TALK_UP);
}
static void pairing(void) {
    dou_wheel_t s = {0};
    assert(dou_wheel_update(&s, true, 0, false, true) == DOU_WHEEL_NONE);
    assert(dou_wheel_update(&s, true, 1000, false, false) == DOU_WHEEL_NONE);
    assert(dou_wheel_update(&s, false, 1100, false, false) == DOU_WHEEL_NONE);
    assert(dou_wheel_update(&s, true, UINT32_MAX - 100, true, false) == DOU_WHEEL_WAKE);
    assert(dou_wheel_update(&s, true, 199, false, false) == DOU_WHEEL_TALK_DOWN);
    assert(dou_wheel_update(&s, false, 200, false, false) == DOU_WHEEL_TALK_UP);
}
static void pages(void) {
    char text[256];
    const char *reply = "the quick brown fox jumps over the lazy dog";
    assert(dou_chat_caption_page(reply, 0, text, sizeof(text)) == 4);
    assert(!strcmp(text, "the quick\nbrown fox"));
    assert(dou_chat_caption_page(reply, 1, text, sizeof(text)) == 4);
    assert(!strcmp(text, "brown fox\njumps over"));
    assert(dou_chat_caption_page(reply, 999, text, sizeof(text)) == 4);
    assert(!strcmp(text, "the lazy\ndog"));
    assert(dou_chat_caption_page(reply, -99, text, sizeof(text)) == 4);
    assert(!strcmp(text, "the quick\nbrown fox"));
    assert(!dou_chat_caption_page("", 0, text, sizeof(text)) && !text[0]);
    assert(!dou_chat_caption_page(reply, 0, text, 0));
    cols = 4; lines = 2;
    assert(dou_chat_caption_page("一二三四五，六七八九十", 0, text, sizeof(text)) == 2);
    assert(!strcmp(text, "一二三四\n五，六七"));
    assert(dou_chat_caption_page("一二三四五，六七八九十", 1, text, sizeof(text)) == 2);
    assert(!strcmp(text, "五，六七\n八九十"));
    cols = 10; lines = 1;
    assert(dou_chat_caption_page(reply, 2, text, sizeof(text)) == 5);
    assert(!strcmp(text, "jumps over"));
}
/* A full Watcher read page of CJK (18 x 9, three bytes a character) is more
 * than the 400-byte caption: pages hold fewer lines, and none is skipped. */
static void cjk_capacity(void) {
    cols = 18; lines = 9;
    static char reply[3 * 400 + 1];
    char *w = reply;
    for (unsigned cp = 0x4e00; cp < 0x4e00 + 400; cp++) {
        *w++ = (char)(0xe0 | cp >> 12);
        *w++ = (char)(0x80 | (cp >> 6 & 0x3f));
        *w++ = (char)(0x80 | (cp & 0x3f));
    }
    *w = 0;
    char page[400], joined[sizeof(reply)] = "", last[64] = "";
    int total = dou_chat_caption_page(reply, 0, page, sizeof(page));
    assert(total > 1);
    for (int i = 0; i < total; i++) {
        assert(dou_chat_caption_page(reply, i, page, sizeof(page)) == total);
        assert(strlen(page) < sizeof(page));
        char *line = page, *next;
        for (int n = 0; line; n++, line = next) {
            next = strchr(line, '\n');
            if (next) *next++ = 0;
            if (n == 0 && i) {
                assert(!strcmp(line, last));   /* each page starts with the last one's last line */
                continue;
            }
            strcat(joined, line);
            strlcpy(last, line, sizeof(last));
        }
    }
    assert(!strcmp(joined, reply));   /* every line shown, in order */
}
static void snapshot(void) {
    dou_review_snapshot_t state;
    static char text[DOU_REVIEW_CAP];
    dou_review_snapshot(text, sizeof(text), &state);
    assert(state.failed && !state.available);
    assert(dou_review_init() == ESP_OK);
    dou_review_snapshot(text, sizeof(text), &state);
    assert(!state.failed && !state.available && !state.revision);
    fail_alloc = 1;
    uint32_t turn = dou_review_turn();
    dou_review_store(turn, "lost allocation");
    dou_review_snapshot(text, sizeof(text), &state);
    assert(state.failed && !state.available);
    fail_alloc = 0;
    dou_review_store(turn, "first");
    dou_review_store(turn, "second");
    dou_review_snapshot(text, sizeof(text), &state);
    assert(state.available && !state.failed && !state.shortened);
    assert(!strcmp(text, "first\n\nsecond") && !dou_review_active());
    uint32_t revision = state.revision;
    dou_review_step(1);
    dou_review_close();
    dou_review_snapshot(NULL, 0, &state);
    assert(state.revision == revision); // navigation isn't new content
    dou_review_store(dou_review_turn(), "");
    dou_review_snapshot(NULL, 0, &state);
    assert(state.revision == revision);
    static char large[DOU_REVIEW_CAP + 8];
    memset(large, 'a', DOU_REVIEW_CAP - 2);
    strcpy(large + DOU_REVIEW_CAP - 2, "界");
    dou_review_store(dou_review_turn(), large);
    dou_review_snapshot(text, sizeof(text), &state);
    assert(state.shortened && strlen(text) == DOU_REVIEW_CAP - 2);
    dou_review_store(dou_review_turn(), "new");
    dou_review_snapshot(text, sizeof(text), &state);
    assert(!state.shortened && !strcmp(text, "new"));
    dou_review_store_complete(dou_review_turn(), "receiver cut", true);
    dou_review_snapshot(text, sizeof(text), &state);
    assert(state.shortened);
}
static void retained(void) {
    cols = 10; lines = 2;
    assert(!dou_review_step(1));
    assert(dou_review_init() == ESP_OK);
    assert(!dou_review_step(1));
    dou_review_store(1, "the quick brown fox jumps over the lazy dog");
    assert(dou_review_step(-1));
    char text[256]; int page, total;
    assert(dou_review_page(text, sizeof(text), &page, &total));
    assert(page == 1 && total == 4);
    assert(dou_review_step(1));
    assert(dou_review_page(text, sizeof(text), &page, &total));
    assert(page == 2 && !strcmp(text, "brown fox\njumps over"));
    for (int i = 0; i < 10; i++) dou_review_step(-1);
    assert(dou_review_page(text, sizeof(text), &page, &total) && page == 1);
    for (int i = 0; i < 10; i++) dou_review_step(1);
    assert(dou_review_page(text, sizeof(text), &page, &total) && page == total);
    dou_review_close();
    assert(!dou_review_active());
    assert(dou_review_step(1));
    assert(dou_review_page(text, sizeof(text), &page, &total) && page == 1);
    dou_review_store(2, "new reply");
    assert(!dou_review_active());
    dou_review_store(3, ""); // no response: the last reply remains available
    dou_review_store(2, "second part");
    assert(dou_review_step(1));
    assert(dou_review_page(text, sizeof(text), &page, &total));
    assert(!strcmp(text, "new reply\nsecond"));
    dou_review_step(1);
    assert(dou_review_page(text, sizeof(text), &page, &total));
    assert(!strcmp(text, "second\npart"));
    dou_review_store(4, "“café”");
    assert(dou_review_step(1));
    assert(dou_review_page(text, sizeof(text), &page, &total));
    assert(text[0] == '"' && !strncmp(text + 1, "cafe", 4) && text[5] == '"' && !text[6]);
}
/* The screen's review copy: closing review, or sleeping (which closes it),
 * hands the label back to the live caption, which review never writes. */
static void view(void) {
    cols = 10; lines = 2;
    dou_review_init();
    dou_review_close();
    dou_review_view_t v = {0};
    char out[256] = "live caption";
    bool changed;
    assert(!dou_review_frame(&v, out, sizeof(out), &changed) && !changed);
    dou_review_store(dou_review_turn(), "the quick brown fox jumps over the lazy dog");
    assert(!dou_review_frame(&v, out, sizeof(out), &changed) && !changed);
    assert(!strcmp(out, "live caption"));
    assert(dou_review_step(1));
    assert(dou_review_frame(&v, out, sizeof(out), &changed) && changed);
    assert(!strcmp(out, "the quick\nbrown fox") && v.page == 1 && v.pages == 4);
    assert(dou_review_frame(&v, out, sizeof(out), &changed) && !changed);   /* nothing to redraw */
    assert(dou_review_step(1));
    assert(dou_review_frame(&v, out, sizeof(out), &changed) && changed);
    assert(!strcmp(out, "brown fox\njumps over") && v.page == 2);
    dou_review_close();
    assert(!dou_review_frame(&v, out, sizeof(out), &changed) && changed);   /* back to the live caption */
    assert(!dou_review_frame(&v, out, sizeof(out), &changed) && !changed);
    assert(dou_review_step(1));   /* reopens on the first page */
    assert(dou_review_frame(&v, out, sizeof(out), &changed) && changed && v.page == 1);
    uint32_t turn = dou_review_turn();
    dou_review_store(turn, "new reply");   /* a new turn's reply closes review */
    assert(!dou_review_frame(&v, out, sizeof(out), &changed) && changed);
    assert(dou_review_step(1));
    assert(dou_review_frame(&v, out, sizeof(out), &changed) && changed && !strcmp(out, "new reply"));
    dou_review_store(turn, "more");   /* the same turn's next message updates the page */
    assert(dou_review_frame(&v, out, sizeof(out), &changed) && changed && !strcmp(out, "new reply\nmore"));
    assert(dou_review_turn() != turn);
    cols = 5;   /* a new layout wraps the same page again */
    v.stale = true;
    assert(dou_review_frame(&v, out, sizeof(out), &changed) && changed && !strcmp(out, "new\nreply"));
    v.stale = true;   /* and after review, still hands back the live caption */
    dou_review_close();
    assert(!dou_review_frame(&v, out, sizeof(out), &changed) && changed);
}
/* Turns before the first paging aren't lost to the old page count. */
static void fast_turns(void) {
    cols = 10; lines = 2;
    dou_review_init();
    dou_review_store(dou_review_turn(), "the quick brown fox jumps over the lazy dog");
    assert(dou_review_step(1));   /* opens on page 1 */
    assert(dou_review_step(1) && dou_review_step(1));
    char text[256]; int page, total;
    assert(dou_review_page(text, sizeof(text), &page, &total) && page == 3 && total == 4);
    for (int i = 0; i < 9; i++) dou_review_step(1);
    assert(dou_review_page(text, sizeof(text), &page, &total) && page == 4);
    dou_review_step(-1);   /* known count now: one back is page 3 */
    assert(dou_review_page(text, sizeof(text), &page, &total) && page == 3);
}
static void long_reply(void) {
    cols = 10; lines = 2;
    char *large = malloc(40000);
    memset(large, 'a', 39999); large[39999] = 0;
    dou_review_init();
    dou_review_store(1, large);
    assert(dou_review_step(1));
    char page[256]; int at, total;
    assert(dou_review_page(page, sizeof(page), &at, &total));
    assert(total > 3000); // full reply, rather than the 1024-byte live-caption buffer
    for (int i = 0; i < total + 5; i++) dou_review_step(1);
    assert(dou_review_page(page, sizeof(page), &at, &total) && at == total);
    free(large);
}

#define DOU_BTN_TALK_PRESS 1
#define DOU_BTN_TALK_RELEASE 2
#define DOU_BTN_TALK_HELD (1u << 17)
#define DOU_BTN_REVIEW_PREV (1u << 15)
#define DOU_BTN_REVIEW_NEXT (1u << 16)
#define portTICK_PERIOD_MS 1
#define ESP_LOGI(...) ((void)0)
static bool asleep, confirm, armed;
static uint32_t tick;
static int talks, releases, cameras, confirmations;
static uint32_t xTaskGetTickCount(void) { return tick; }
static int64_t esp_timer_get_time(void) { return (int64_t)tick * 1000; }
/* `confirm`: the next press confirms; `armed`: a pairing prompt waits for a press. */
static bool dou_link_confirm_press(int64_t released_us) {
    (void)released_us;
    bool c = confirm || armed;
    confirm = armed = false;
    confirmations += c;
    return c;
}
static bool dou_state_asleep(void) { return asleep; }
static void dou_state_poke(void) {}
typedef enum { DOU_MODE_IDLE = 1, DOU_MODE_ERROR = 5 } dou_mode_t;
static dou_mode_t mode = DOU_MODE_IDLE;
static char caption[64];
static dou_mode_t dou_state_mode(float *since) { (void)since; return mode; }
static void dou_state_set_caption(const char *text) { strlcpy(caption, text, sizeof(caption)); }
static void set_asleep(bool value, const char *why) { (void)why; asleep = value; }
typedef enum { DOU_PTT_DOWN, DOU_PTT_UP } dou_ptt_t;
static void post(dou_ptt_t type, bool wake) { (void)wake; talks += type == DOU_PTT_DOWN; releases += type == DOU_PTT_UP; }
static int fake_audio(void) { return 0; }
static const struct { int (*audio_init)(void), (*mic_init)(void); bool keyboard; const char *talk_button; } board = {
    .audio_init = fake_audio, .talk_button = "wheel" }, *dou_board = &board;
enum { DOU_MENU_SELECT };
static bool dou_menu_is_open(void) { return false; }
static void dou_menu_key(int key) { (void)key; }
static bool s_talk_down;
static int64_t s_talk_released_us;
''' + talk + r'''
#if CONFIG_DOU_WATCHER_CAMERA
static void watcher_camera_preview_toggle(void) { cameras++; }
#endif
''' + watcher + r'''
static void input(unsigned ev, uint32_t now) { tick = now; watcher_buttons(ev); }
static void routing(void) {
    dou_review_init();
    input(1, 100); input(2, 200); input(0, 600);
    assert(asleep && !talks && !releases);
    input(1, 700); input(2, 800);
    assert(!asleep && !talks);
    input(1, 1000); input(2, 1100);
    input(1, 1200); input(2, 1250); input(0, 1600);
    assert(!asleep && !talks);
    assert(cameras == CONFIG_DOU_WATCHER_CAMERA);
    input(1, 2000); input(0, 2299);
    assert(!talks);
    input(0, 2300); input(2, 2800);
    assert(talks == 1 && releases == 1 && !asleep);
    confirm = true;
    input(1, 3000); input(0, 4000); input(2, 4100);
    assert(talks == 1 && releases == 1 && !asleep);
    dou_review_store(1, "the quick brown fox jumps over the lazy dog");
    input(DOU_BTN_REVIEW_NEXT, 4200);
    assert(dou_review_active() && talks == 1);
}
/* A pairing prompt that appears while the wheel is already held (a BLE peer
 * finishing its handshake just after the press) waits for a press of its own:
 * the hold that started before it only talks. */
static void late_prompt(void) {
    dou_review_init();
    input(1, 0);   /* the press edge: nothing to confirm yet */
    assert(!confirmations);
    armed = true;  /* the prompt arrives mid-hold */
    input(0, 100); input(0, 300);   /* the hold becomes push-to-talk */
    assert(!confirmations && armed && talks == 1);
    input(2, 400);
    assert(releases == 1 && armed);
    input(1, 1000);   /* a fresh press confirms it, and doesn't talk */
    assert(confirmations == 1 && !armed);
    input(0, 1400); input(2, 1500);
    assert(talks == 1 && releases == 1);
}
/* Let go on the very poll the hold would start: a click, not a press and release. */
static void threshold(void) {
    dou_review_init();
    input(1, 0); input(2, 300);
    assert(!talks && !releases);
    input(0, 700);
    assert(asleep);
    input(1, 1000); input(2, 1050);   /* wakes */
    assert(!asleep && !talks);
}
/* Turning with nothing to review says so, but not over an error caption. */
static void nothing_to_review(void) {
    dou_review_init();
    strcpy(caption, "CAN'T REACH DOU");
    mode = DOU_MODE_ERROR;
    input(DOU_BTN_REVIEW_NEXT, 100);
    assert(!strcmp(caption, "CAN'T REACH DOU") && !dou_review_active());
    mode = DOU_MODE_IDLE;
    input(DOU_BTN_REVIEW_NEXT, 200);
    assert(!strcmp(caption, "NO REPLY YET"));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "click")) click();
    else if (!strcmp(argv[1], "pairing")) pairing();
    else if (!strcmp(argv[1], "pages")) pages();
    else if (!strcmp(argv[1], "cjk")) cjk_capacity();
    else if (!strcmp(argv[1], "retained")) retained();
    else if (!strcmp(argv[1], "snapshot")) snapshot();
    else if (!strcmp(argv[1], "long")) long_reply();
    else if (!strcmp(argv[1], "view")) view();
    else if (!strcmp(argv[1], "fast")) fast_turns();
    else if (!strcmp(argv[1], "routing")) routing();
    else if (!strcmp(argv[1], "threshold")) threshold();
    else if (!strcmp(argv[1], "empty")) nothing_to_review();
    else if (!strcmp(argv[1], "late")) late_prompt();
    else return 2;
    return 0;
}
''')
        cls.binary = out / "controls"
        command = [
            *shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DCONFIG_DOU_BOARD_SENSECAP_WATCHER=1",
            "-include", str(ROOT / "tests/host_compat.h"), "-I", str(out), "-I", str(DOU),
            str(harness), str(DOU / "dou_review.c"), str(DOU / "dou_chat_text.c"),
            str(DOU / "dou_text.c"), "-o", str(cls.binary),
        ]
        for camera in (0, 1):
            binary = out / ("controls" if not camera else "controls-camera")
            result = subprocess.run(command[:-1] + [str(binary), f"-DCONFIG_DOU_WATCHER_CAMERA={camera}"],
                                    capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
        cls.camera_binary = out / "controls-camera"

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_click_sleeps_and_hold_records(self):
        self.run_case("click")

    def test_pairing_wake_and_clock_wrap(self):
        self.run_case("pairing")

    def test_page_boundaries_and_utf8(self):
        self.run_case("pages")

    def test_cjk_pages_fit_the_caption_and_skip_no_line(self):
        self.run_case("cjk")

    def test_review_retains_and_replaces_reply(self):
        self.run_case("retained")

    def test_review_handles_long_reply(self):
        self.run_case("long")

    def test_closing_review_returns_the_live_caption(self):
        self.run_case("view")

    def test_turns_before_the_first_paging_count(self):
        self.run_case("fast")

    def test_screen_keeps_review_apart_from_the_live_caption(self):
        source = (DOU / "dou_ui.c").read_text()
        start = source.index("static void update_status(")
        body = source[start:source.index("\nstatic volatile bool s_snapshot;", start)]
        self.assertFalse("dou_review_page(caption" in body, "review must not write the live caption")
        self.assertTrue("dou_review_frame(&view, review, sizeof(review)" in body)


    def test_input_routes_click_hold_pairing_and_rotation(self):
        self.run_case("routing")

    def test_release_on_the_hold_poll_is_a_click(self):
        for binary in (self.binary, self.camera_binary):
            result = subprocess.run([str(binary), "threshold"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_hold_started_before_a_pairing_prompt_does_not_confirm_it(self):
        for binary in (self.binary, self.camera_binary):
            result = subprocess.run([str(binary), "late"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_nothing_to_review_keeps_an_error_caption(self):
        self.run_case("empty")

    def test_camera_double_click_does_not_record_or_sleep(self):
        result = subprocess.run([str(self.camera_binary), "routing"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_snapshots_failures_revisions_and_utf8_truncation(self):
        self.run_case("snapshot")
