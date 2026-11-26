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
#include <stdarg.h>
#include "dou_input.c"

struct test_queue { dou_input_event_t items[32]; int n; } queue;
static bool asleep, call, notes_mode, ble, menu, pairing, queue_full, speaker = true;
static int slept, toggled, powered_off, shutdown, selected, speaker_toggles;
static int64_t clock_us;
static char caption[96];
static dou_mode_t mode = DOU_MODE_IDLE;
BaseType_t xQueueSend(QueueHandle_t q,const void *p,TickType_t t) {
    (void)t; if (queue_full) return 0;
    assert(q->n<32); q->items[q->n++]=*(const dou_input_event_t *)p; return pdTRUE;
}
bool dou_settings_speaker_on(void) { return speaker; }
void dou_settings_set_speaker_on(bool v) { speaker=v; ++speaker_toggles; }
void dou_state_set_asleep(bool v) { asleep=v; slept+=v; }
bool dou_state_asleep(void) { return asleep; }
bool dou_state_on_battery(void) { return false; }
void dou_state_poke(void) {}
void dou_state_set_level(float v) { (void)v; }
void dou_state_set_progress(float v) { (void)v; }
void dou_state_set_mode(dou_mode_t v) { mode=v; }
dou_mode_t dou_state_mode(float *v) { if(v) *v=0; return mode; }
static float idle_secs = 1000;
float dou_state_idle_secs(void) { return idle_secs; }
void dou_state_set_caption(const char *fmt,...) {
    va_list ap; va_start(ap,fmt); vsnprintf(caption,sizeof(caption),fmt,ap); va_end(ap);
}
void dou_state_set_hint(const char *text) { snprintf(caption,sizeof(caption),"%s",text); }
bool dou_state_hint_pending(void) { return false; }
bool dou_state_caption(char *p,size_t n,uint32_t *v) { (void)v; snprintf(p,n,"%s",caption); return true; }
bool dou_voice_in_call(void) { return call; }
bool dou_voice_ptt_enabled(void) { return CONFIG_DOU_BOARD_M5STACK_STOPWATCH && CONFIG_DOU_REALTIME_VOICE && !notes_mode; }
bool dou_voice_resting(void) { return false; }
void dou_voice_shutdown(int ms) { assert(ms==3000); ++shutdown; call=false; }
void dou_voice_cancel_shutdown(void) { assert(powered_off); }
bool dou_wifi_connected(void) { return true; }
void dou_wifi_apply(void) {}
bool dou_console_host(void) { return true; }
bool dou_ui_dark(void) { return false; }
void xTaskNotifyGive(TaskHandle_t t) { (void)t; }
void vTaskDelay(int ms) { clock_us+=ms*1000; }
TickType_t xTaskGetTickCount(void) { return clock_us/1000; }
int64_t esp_timer_get_time(void) { return clock_us; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "failed"; }
bool dou_settings_ble_on(void) { return ble; }
void dou_settings_set_ble_on(bool v) { ble=v; ++toggled; }
int dou_settings_sleep_s(void) { return 120; }
void dou_ble_status(dou_ble_status_t *p) { memset(p,0,sizeof(*p)); }
dou_link_state_t dou_link_state(void) { return DOU_LINK_ONLINE; }
bool dou_link_talk_press(void) { return pairing; }
bool dou_link_confirm_press(int64_t released_us) { (void)released_us; return pairing; }
bool dou_menu_is_open(void) { return menu; }
void dou_menu_key(dou_menu_key_t key) { selected=key; }
static esp_err_t board_off(void) { assert(shutdown==1); ++powered_off; return ESP_FAIL; }
/* A two-button board with AUX (every board but the StopWatch), and the
 * StopWatch as it ships: talk, speaker and red POWER, each its own control. */
static esp_err_t board_mic(void) { return ESP_OK; }
static const dou_board_t board={.touch=true,.talk_button="yellow",.aux_button="blue",.power_off=board_off,
    .mic_init=board_mic};
static const dou_board_t stopwatch_board={.touch=true,.rim_controls=true,.talk_button="top-left",
    .speaker_button="top-right",.power_button="red bottom-left",.power_off=board_off,.mic_init=board_mic};
/* No speaker or microphone (the Waveshare 1.43, LCD-1.28): no voice task. */
static const dou_board_t display_board={.touch=true,.talk_button="boot",.power_off=board_off};
/* No touch: the aux button opens the menu (the AIPI). */
static const dou_board_t keys_board={.talk_button="bottom right",.aux_button="bottom left",.power_off=board_off,
    .mic_init=board_mic};
const dou_board_t *dou_board=&board;

static void tap(void) { aux_button(true,true); aux_button(false,true); }
static void ticks(int n) { for(int i=0;i<n;i++) aux_button(false,false); }
/* The production poll path: dedicated_buttons() runs once per 10 ms poll. */
static void blue(void) { dedicated_buttons(DOU_BTN_SPEAKER_PRESS); dedicated_buttons(DOU_BTN_SPEAKER_RELEASE); }
static void polls(int n) { for(int i=0;i<n;i++) dedicated_buttons(0); }
static int ends(void) { int n=0; for(int i=0;i<queue.n;i++) n+=queue.items[i].type==DOU_PTT_END_CALL; return n; }
int main(int argc,char **argv) {
    assert(argc==2); int scenario=atoi(argv[1]); s_queue=&queue;
    bool stopwatch=CONFIG_DOU_BOARD_M5STACK_STOPWATCH && CONFIG_DOU_REALTIME_VOICE;
    if(scenario==1) {
        tap(); assert(!slept); ticks(DOUBLE_TICKS); assert(slept==1 && asleep);
        tap(); ticks(DOUBLE_TICKS); assert(!asleep && slept==1 && !toggled);
    } else if(scenario==2) {
        tap(); ticks(4); tap(); ticks(DOUBLE_TICKS); assert(toggled==1 && ble && !slept);
    } else if(scenario==3) {
        aux_button(true,true);
        for(int i=0;i<LONG_TICKS;i++) aux_button(true,false);
        assert(powered_off==1 && shutdown==1 && !slept);
    } else if(scenario==4) {
        /* In a call blue hangs up on the press: the speaker setting is untouched,
         * and a quick second press (a double tap) doesn't toggle it either. */
        dou_board=&stopwatch_board;
        call=true; dedicated_buttons(DOU_BTN_SPEAKER_PRESS); call=false;
        assert(queue.n==1 && queue.items[0].type==DOU_PTT_END_CALL && speaker_toggles==0);
        dedicated_buttons(DOU_BTN_SPEAKER_RELEASE); polls(4); blue();
        assert(speaker_toggles==0 && queue.n==1);
        polls(DOUBLE_TICKS); blue(); assert(speaker_toggles==1 && !speaker && queue.n==1);   /* its own role again */
    } else if(scenario==5) {
        /* A talk press the voice task hasn't taken up yet: blue already ends
         * the call it would start; once it's settled, blue mutes again. */
        dou_board=&stopwatch_board;
        talk_button(DOU_BTN_TALK_PRESS);
        assert(queue.n==1 && queue.items[0].type==DOU_PTT_DOWN && (atomic_load(&s_call_pending)!=0)==(bool)stopwatch);
        blue();
        assert(stopwatch ? (ends()==1 && speaker_toggles==0) : (!ends() && speaker_toggles==1));
        dou_input_clear_call_pending(queue.items[0].mark); polls(DOUBLE_TICKS); blue();
        assert(speaker_toggles==(stopwatch ? 1 : 2) && ends()==(stopwatch ? 1 : 0));
    } else if(scenario==6) {
        /* A press that wakes the screen counts as a call press too. */
        dou_board=&stopwatch_board;
        asleep=true; talk_button(DOU_BTN_TALK_PRESS);
        assert(!asleep && queue.n==1 && queue.items[0].wake);
        assert((atomic_load(&s_call_pending)!=0)==(bool)stopwatch);
        blue(); assert(stopwatch ? (ends()==1 && speaker_toggles==0) : speaker_toggles==1);
    } else if(scenario==7) {
        asleep=true; talk_button(DOU_BTN_TALK_PRESS|DOU_BTN_TALK_RELEASE);
        assert(!asleep && queue.n==2 && queue.items[0].wake && !queue.items[1].wake);
        queue.n=0; menu=true; talk_button(DOU_BTN_TALK_PRESS|DOU_BTN_TALK_RELEASE);
        assert(!queue.n && selected==DOU_MENU_SELECT);
        menu=false; pairing=true; talk_button(DOU_BTN_TALK_PRESS|DOU_BTN_TALK_RELEASE); assert(!queue.n);
    } else if(scenario==8) {
        /* The routing is StopWatch-with-calls only; other builds keep mute. */
        dou_board=&stopwatch_board;
        call=true; blue(); polls(DOUBLE_TICKS*2);
        assert(stopwatch ? (ends()==1 && speaker_toggles==0) : (!queue.n && speaker_toggles==1));
        /* Legacy AUX boards: a press in a call still sleeps (no call role). */
        dou_board=&board; queue.n=0;
        tap(); ticks(DOUBLE_TICKS*2); assert(!queue.n && slept==1);
    } else if(scenario==10) {
        /* A talk press within a call is a new hold, not a pending call. */
        dou_board=&stopwatch_board;
        call=true; talk_button(DOU_BTN_TALK_PRESS|DOU_BTN_TALK_RELEASE);
        assert(!(atomic_load(&s_call_pending)!=0));
        call=false; blue(); assert(speaker_toggles==1 && !ends());
    } else if(scenario==14) {
        /* Each pending press owns its mark: clearing an older one (the voice
         * task finishing with it) leaves a newer press's mark, so blue still
         * hangs up the call that one will start. */
        dou_board=&stopwatch_board;
        talk_button(DOU_BTN_TALK_PRESS); talk_button(DOU_BTN_TALK_RELEASE);
        uint32_t first=queue.items[0].mark;
        talk_button(DOU_BTN_TALK_PRESS);
        uint32_t second=queue.items[2].mark;
        assert(stopwatch ? (first && second && first!=second) : (!first && !second));
        dou_input_clear_call_pending(first);
        assert((atomic_load(&s_call_pending)!=0)==(bool)stopwatch);
        blue(); assert(stopwatch ? (ends()==1 && !speaker_toggles) : speaker_toggles==1);
        dou_input_clear_call_pending(second); assert(!atomic_load(&s_call_pending));
        dou_input_clear_call_pending(0); assert(!atomic_load(&s_call_pending));
    } else if(scenario==11) {
        /* A hang-up that can't be queued still ends the call: the input epoch
         * moves, which the voice task treats as hung up, and nothing is pending. */
        dou_board=&stopwatch_board;
        uint32_t epoch=dou_input_call_epoch();
        call=true; queue_full=true; blue(); queue_full=false;
        assert(!queue.n && speaker_toggles==(stopwatch ? 0 : 1));
        assert(stopwatch ? dou_input_call_epoch()!=epoch : dou_input_call_epoch()==epoch);
        assert(!(atomic_load(&s_call_pending)!=0));
    } else if(scenario==12) {
        /* Screen asleep mid-call (red short press): blue wakes it and hangs up. */
        dou_board=&stopwatch_board;
        call=true;
        dedicated_buttons(DOU_BTN_POWER_PRESS); dedicated_buttons(DOU_BTN_POWER_RELEASE);
        assert(asleep && !queue.n);   /* the call goes on with the screen off */
        blue(); assert(stopwatch ? (!asleep && ends()==1 && speaker_toggles==0) : (!asleep && speaker_toggles==0));
    } else if(scenario==13) {
        /* Red held in a call: the talk hold is released and the call ended
         * (dou_voice_shutdown) before power goes. */
        dou_board=&stopwatch_board;
        call=true; talk_button(DOU_BTN_TALK_PRESS); assert(s_talk_down);
        dedicated_buttons(DOU_BTN_POWER_PRESS); dedicated_buttons(DOU_BTN_POWER_LONG);
        assert(!s_talk_down && shutdown==1 && powered_off==1 && !call);
        assert(queue.items[queue.n-1].type==DOU_PTT_UP);
    } else if(scenario==16) {
        /* Display-only: nothing reads the queue, so a talk press never posts.
         * It still wakes the screen, selects in the menu and confirms pairing. */
        dou_board=&display_board;
        asleep=true; talk_button(DOU_BTN_TALK_PRESS|DOU_BTN_TALK_RELEASE);
        assert(!asleep && !queue.n && !s_talk_down);
        for(int i=0;i<40;i++) { talk_button(DOU_BTN_TALK_PRESS); talk_button(DOU_BTN_TALK_RELEASE); }
        assert(!queue.n && !s_talk_down && !atomic_load(&s_call_pending));
        menu=true; talk_button(DOU_BTN_TALK_PRESS|DOU_BTN_TALK_RELEASE);
        assert(!queue.n && selected==DOU_MENU_SELECT);
        menu=false; pairing=true; talk_button(DOU_BTN_TALK_PRESS|DOU_BTN_TALK_RELEASE); assert(!queue.n);
#if CONFIG_DOU_VOICE_NOTE_TOGGLE
    } else if(scenario==17) {
        /* A toggled note records with talk let go: no button hides it. */
        dou_input_note_toggled(true);
        tap(); ticks(DOUBLE_TICKS*2); assert(!asleep && !slept);              /* aux: no sleep */
        dedicated_buttons(DOU_BTN_POWER_PRESS); dedicated_buttons(DOU_BTN_POWER_RELEASE);
        assert(!asleep && !slept);                                           /* power: no sleep */
        dou_board=&keys_board; selected=-1;
        menu_button(true,true); menu_button(false,true); assert(selected==-1);   /* no menu */
        talk_button(DOU_BTN_TALK_PRESS|DOU_BTN_TALK_RELEASE);
        assert(selected==-1 && queue.n==2 && queue.items[0].type==DOU_PTT_DOWN);   /* stops the note */
        dou_input_note_toggled(false);
        dou_board=&board; tap(); ticks(DOUBLE_TICKS); assert(asleep && slept==1);   /* sleeps again */
#endif
    } else if(scenario==9) {
        call=true; check_sleep(); assert(!asleep); call=false; check_sleep(); assert(asleep);
    } else if(scenario==15) {
        /* Auto-sleep (#281): fresh activity must not sleep, active modes
         * must not sleep, and a stale clock in IDLE still sleeps. */
        idle_secs=10; check_sleep(); assert(!asleep);   /* just poked (e.g. by a turn) */
        mode=DOU_MODE_SPEAKING; idle_secs=1000; check_sleep(); assert(!asleep);
        mode=DOU_MODE_THINKING; check_sleep(); assert(!asleep);
        mode=DOU_MODE_LISTENING; check_sleep(); assert(!asleep);
        mode=DOU_MODE_IDLE; check_sleep(); assert(asleep);   /* truly idle: sleeps */
    }
    return 0;
}
