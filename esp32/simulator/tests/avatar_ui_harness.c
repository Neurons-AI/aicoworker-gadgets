/* Copyright (c) Meta Platforms, Inc. and affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <assert.h>
#include "sim_board.h"
#include "sim_platform.h"
#include "sim_services.h"
#include "dou_avatar.h"
#include "../../components/dou/dou_ui.c"

static dou_avatar_source_t selected;
static uint16_t pixels[16] = {0xf800, 0x07e0, 0x001f, 0xffff};
static dou_avatar_image_t personal = {.data={pixels,4,4},.revision=1};
static unsigned refs;
void dou_avatar_start(void) {}
dou_avatar_source_t dou_avatar_source(void) { return selected; }
void dou_avatar_select(dou_avatar_source_t source) { selected = source; }
const dou_avatar_image_t *dou_avatar_acquire(void) {
    if (selected != DOU_AVATAR_PERSONAL) return NULL;
    refs++; return &personal;
}
void dou_avatar_release(const dou_avatar_image_t *image) { if(image) { assert(image == &personal && refs); refs--; } }

static lv_point_t finger;
static bool down;
static void read_touch(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev; data->point=finger; data->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
static void touch(int x,int y,bool pressed) {
    finger=(lv_point_t){x,y};down=pressed;sim_time_advance_us(20000);
    lv_indev_read(s_indev);lv_obj_update_layout(lv_screen_active());
}
int main(void) {
    sim_time_reset(); sim_services_reset(); lv_init(); dou_state_init();
    dou_board=sim_board_get(); assert(dou_ui_start()==ESP_OK);
    dou_state_set_asleep(false); dou_state_set_mode(DOU_MODE_IDLE);
    lv_indev_set_read_cb(s_indev,read_touch);lv_obj_update_layout(lv_screen_active());
    frame_tick(NULL); assert(!s_avatar_visible && !refs);
    dou_avatar_select(DOU_AVATAR_PERSONAL);
    frame_tick(NULL); assert(s_avatar_visible && s_avatar_revision==1 && !refs);
    lv_image_decoder_dsc_t decoder={.header=s_dou_src.header};
    assert(dou_dec_open(NULL,&decoder)==LV_RESULT_OK && refs==1);
    /* Source changes do not release a frame held by an open strip decoder. */
    dou_avatar_select(DOU_AVATAR_BUILTIN);
    lv_area_t full={0,0,s_canvas_px-1,s_canvas_px-1},area={.y1=LV_COORD_MIN};
    assert(dou_dec_get_area(NULL,&decoder,&full,&area)==LV_RESULT_OK);
    dou_dec_close(NULL,&decoder);assert(!refs);
    frame_tick(NULL);assert(!s_avatar_visible);
    dou_avatar_select(DOU_AVATAR_PERSONAL);frame_tick(NULL);
    int x=s_w/2,y=s_h/2;
    touch(x,y,true);touch(x,y,false);assert(dou_state_happiness()>0);
    touch(x,y,true);touch(x,y-80,true);assert(dou_ui_volume_swiping());
    touch(x,y-80,false);assert(!dou_ui_volume_swiping());
    dou_state_set_mode(DOU_MODE_LISTENING);frame_tick(NULL);assert(s_avatar_visible);
    dou_state_set_mode(DOU_MODE_SPEAKING);dou_state_set_level(1);frame_tick(NULL);assert(s_avatar_visible);
    dou_state_set_asleep(true);frame_tick(NULL);
    dou_state_set_asleep(false);dou_state_set_mode(DOU_MODE_IDLE);frame_tick(NULL);assert(s_avatar_visible);
    for(int i=0;i<200;i++) {
        dou_avatar_select(i%2?DOU_AVATAR_PERSONAL:DOU_AVATAR_BUILTIN);
        frame_tick(NULL);
        assert(s_avatar_visible==(i%2!=0) && !refs);
    }
    lv_deinit();assert(!refs);
    puts("avatar UI: default, runtime switch, pinned strip lifetime, touch/pet, volume, voice states, sleep/wake passed");
}
