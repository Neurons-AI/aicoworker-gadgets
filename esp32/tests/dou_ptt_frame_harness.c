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
#include <assert.h>
#include <math.h>
#include "dou_voice.h"
#include "dou_pixel.h"

typedef void lv_timer_t;
static bool s_snapshot;
static float s_level;
static dou_mode_t s_last_mode=DOU_MODE_COUNT, mode;
static dou_mode_t s_last_epaper_pose=DOU_MODE_COUNT;
static const struct { bool epaper; } board = { false }, *dou_board = &board;
static dou_voice_view_t view;
static float source_level;
static dou_pose_t rendered;
static bool in_call = true, reply_waiting;
static struct { void *data; } s_image_dsc;
static void *s_tv;
static void send_snapshot(void) {}
static void image_sync(void) {}
static void image_hide_locked(void) {}
static void dou_ui_show_face(void) {}
#if CONFIG_DOU_VOICE_NOTE_TOGGLE
static bool s_note_listening;
static void update_tileview_swipe(void) {}
#endif
static bool update_sleep(void) { return false; }
static void update_chrome(float now) { (void)now; }
static bool dou_menu_tick(float now) { (void)now; return false; }
static int lv_obj_get_scroll_x(void *obj) { (void)obj; return 0; }
static void invalidate_dou(void) {}
static void update_status(dou_mode_t m,float now) { (void)now; assert(m==mode); }
static void update_control_hints(dou_mode_t m) { assert(m==mode); }   /* StopWatch rim icons */
int64_t esp_timer_get_time(void) { return 1000000; }
dou_mode_t dou_state_mode(float *t) { if (t) *t=0; return mode; }
float dou_state_level(void) { return source_level; }
float dou_state_happiness(void) { return 0; }
bool dou_voice_in_call(void) { return in_call; }
bool dou_voice_reply_waiting(void) { return reply_waiting; }
dou_voice_view_t dou_voice_call_view(void) { return view; }
void dou_pixel_render(const dou_pose_t *pose) { rendered=*pose; }

/* The test runner copies the complete production frame_tick function here. */
#include "dou_ptt_frame.inc"

int main(int argc,char **argv)
{
    (void)argv;
    assert(argc==2);
    mode=DOU_MODE_SPEAKING; view=DOU_VOICE_VIEW_SPEAKING; source_level=0.9f;
    frame_tick(NULL);
    assert(rendered.mode==DOU_MODE_SPEAKING && fabsf(rendered.level-0.54f)<0.0001f);
    mode=DOU_MODE_LISTENING; view=DOU_VOICE_VIEW_LISTENING; source_level=0.1f;
    frame_tick(NULL);
    assert(rendered.mode==DOU_MODE_LISTENING && fabsf(rendered.level-0.06f)<0.0001f);
    /* Mode was read just before the voice task switched to speaker output. */
    view=DOU_VOICE_VIEW_SPEAKING; source_level=0.9f;
    frame_tick(NULL);
    assert(fabsf(rendered.level-0.048f)<0.0001f); /* only the previous mic level decays */
    /* A voice-note reply keeps its speaking status/progress while only its
     * avatar rests between audio segments. update_status asserts that mode. */
    in_call=false; mode=DOU_MODE_SPEAKING; view=DOU_VOICE_VIEW_NONE;
    source_level=0; reply_waiting=true;
    frame_tick(NULL);
    assert(rendered.mode==DOU_MODE_IDLE);
    source_level=0.9f; reply_waiting=false;
    frame_tick(NULL);
    assert(rendered.mode==DOU_MODE_SPEAKING);
    return 0;
}
