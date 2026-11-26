/*
 * AICW Stick: the dou_chat.h voice session over a plain WebSocket to the
 * AICW bridge on the LAN (projects/m5sticks3/PROTOCOL.md), instead of Dou's
 * Noise session. Speech goes up as 16 kHz PCM16; the bridge does STT, asks
 * the AICoworker agent and sends the reply text plus TTS speech (16 kHz PCM16)
 * back. Replaces dou_chat_session.cpp when CONFIG_DOU_AICW_BRIDGE is set.
 *
 * The bridge address is the "hatch host" setting ("ip:port"), set over the
 * serial console with >aicw_host=.
 */

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "psa/crypto.h"

#include "dou_chat_priv.h"
#include "dou_settings.h"
#include "dou_wifi.h"
#include "sdkconfig.h"

static const char *TAG = "aicw_chat";

#define RATE 16000
#define IN_BYTES (RATE * 2 * 8)        /* 8 s of mic backlog */
#define OUT_BYTES (RATE * 2 * 6)       /* 6 s of reply speech */
#define RX_CAP (64 * 1024)
#define TX_CHUNK 3200                  /* 100 ms of mic per frame */
#define TEXT_MAX 1024
#define HEARD_TIMEOUT_US (25 * 1000000LL)
#define DONE_TIMEOUT_US (120 * 1000000LL)
#define PING_US (20 * 1000000LL)
#define RETRY_US (3 * 1000000LL)
#define SPEECH_CHARS_PER_S 14

typedef enum { CMD_BEGIN, CMD_END, CMD_CANCEL, CMD_TEXT, CMD_CONNECT, CMD_FORGET, CMD_WAKE, CMD_TEXT_CANCEL } cmd_type_t;
typedef struct {
    cmd_type_t type;
    uint32_t gen;
    char *text;
} cmd_t;

typedef struct {
    dou_hatch_ev_t type;
    uint32_t gen;
    char text[TEXT_MAX];
} ev_t;

static QueueHandle_t s_cmds;
static QueueHandle_t s_events;
static StreamBufferHandle_t s_in;
static StreamBufferHandle_t s_out;
static atomic_uint s_gen;
static atomic_bool s_resting;

/* Network task state */
static int s_sock = -1;
static uint8_t *s_rx;
static size_t s_rx_len;
static uint8_t *s_tx;
static uint32_t s_turn;          /* turn being streamed / answered, 0 = none */
static bool s_sending;           /* mic audio still going up */
static bool s_text_turn;         /* typed turn: reply goes to the console */
static uint32_t s_audio_turn;    /* turn whose speech is arriving */
static int64_t s_heard_by, s_done_by, s_last_tx, s_next_try;

/* Caption state, shared with the voice task */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static char *s_reply;            /* TEXT_MAX */
static uint32_t s_reply_frames;  /* total speech frames, 0 = unknown */
static bool s_reply_audio_done;

static int64_t now_us(void) { return esp_timer_get_time(); }

static void post(cmd_type_t type, uint32_t gen)
{
    if (!s_cmds) return;
    cmd_t c = { type, gen, NULL };
    xQueueSend(s_cmds, &c, pdMS_TO_TICKS(200));
}

static void emit(dou_hatch_ev_t type, uint32_t gen, const char *text)
{
    static ev_t ev;   /* network task only */
    ev.type = type;
    ev.gen = gen;
    strlcpy(ev.text, text ? text : "", sizeof(ev.text));
    if (xQueueSend(s_events, &ev, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropped %d", type);
    }
}

static void drain_out(void)
{
    uint8_t junk[256];
    while (xStreamBufferReceive(s_out, junk, sizeof(junk), 0) > 0) {
    }
}

static void reset_reply(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_reply) s_reply[0] = '\0';
    s_reply_frames = 0;
    s_reply_audio_done = false;
    portEXIT_CRITICAL(&s_lock);
}

#include "aicw_link.inc"

static bool send_json(cJSON *o)
{
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!s) return false;
    bool ok = ws_send(0x1, s, strlen(s));
    cJSON_free(s);
    return ok;
}

static bool send_simple(const char *type, uint32_t id)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", type);
    if (id) cJSON_AddNumberToObject(o, "id", id);
    return send_json(o);
}

static bool ws_connect(void)
{
    if (!link_open()) return false;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macs[13];
    snprintf(macs, sizeof(macs), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "hello");
    cJSON_AddStringToObject(o, "device", "m5stack-sticks3");
    cJSON_AddStringToObject(o, "mac", macs);
    cJSON_AddStringToObject(o, "fw", "aicw-0.2");
    if (!send_json(o)) return false;
    ESP_LOGI(TAG, "connected (%s)", s_relay ? "relay" : "lan");
    dou_hatch_report(DOU_HATCH_REACHABLE, s_relay ? "Đã kết nối AICW (relay)" : "Đã kết nối AICW");
    return true;
}

/* ---- Inbound ---- */

static void on_text(const char *s, size_t len)
{
    cJSON *o = cJSON_ParseWithLength(s, len);
    if (!o) return;
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(o, "type"));
    cJSON *jid = cJSON_GetObjectItem(o, "id");
    uint32_t id = cJSON_IsNumber(jid) ? (uint32_t)jid->valuedouble : 0;
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(o, "text"));
    if (!type) goto out;
    if (!strcmp(type, "ready") || !strcmp(type, "pong")) goto out;
    if (!id || id != s_turn) goto out;   /* a cancelled or older turn */

    if (s_text_turn) {
        if (!strcmp(type, "reply")) {
            dou_hatch_console("text", text ? text : "", "\"msg\":0");
        } else if (!strcmp(type, "done")) {
            dou_hatch_console("done", NULL, "\"messages\":1,\"complete\":true");
            s_turn = 0;
        } else if (!strcmp(type, "error")) {
            dou_hatch_console("error", text ? text : "error", NULL);
            s_turn = 0;
        }
        goto out;
    }
    if (!strcmp(type, "sent")) {
        emit(DOU_HATCH_EV_SENT, id, "");
    } else if (!strcmp(type, "heard")) {
        s_heard_by = 0;
        emit(DOU_HATCH_EV_HEARD, id, text);
    } else if (!strcmp(type, "reply")) {
        portENTER_CRITICAL(&s_lock);
        strlcpy(s_reply, text ? text : "", TEXT_MAX);
        portEXIT_CRITICAL(&s_lock);
        emit(DOU_HATCH_EV_REPLY, id, text);
    } else if (!strcmp(type, "audio_start")) {
        cJSON *f = cJSON_GetObjectItem(o, "frames");
        portENTER_CRITICAL(&s_lock);
        s_reply_frames = cJSON_IsNumber(f) ? (uint32_t)f->valuedouble : 0;
        portEXIT_CRITICAL(&s_lock);
        s_audio_turn = id;
    } else if (!strcmp(type, "audio_end")) {
        portENTER_CRITICAL(&s_lock);
        s_reply_audio_done = true;
        portEXIT_CRITICAL(&s_lock);
        s_audio_turn = 0;
    } else if (!strcmp(type, "done")) {
        emit(DOU_HATCH_EV_DONE, id, "");
        s_turn = 0;
        s_done_by = 0;
    } else if (!strcmp(type, "error")) {
        emit(DOU_HATCH_EV_ERROR, id, text ? text : "Lỗi");
        s_turn = 0;
        s_done_by = 0;
    }
out:
    cJSON_Delete(o);
}

static void on_binary(const uint8_t *data, size_t len)
{
    if (!s_audio_turn || s_audio_turn != s_turn || s_audio_turn != atomic_load(&s_gen)) return;
    len &= ~(size_t)1;
    while (len) {
        size_t n = xStreamBufferSend(s_out, data, len, pdMS_TO_TICKS(200));
        data += n;
        len -= n;
        if (s_audio_turn != atomic_load(&s_gen)) return;   /* cancelled while waiting */
    }
}

/* Parses whole frames out of s_rx. */
static void parse_frames(void)
{
    size_t off = 0;
    while (s_rx_len - off >= 2) {
        uint8_t *p = s_rx + off;
        uint8_t op = p[0] & 0x0f;
        bool masked = p[1] & 0x80;
        uint64_t len = p[1] & 0x7f;
        size_t hl = 2;
        if (len == 126) {
            if (s_rx_len - off < 4) break;
            len = ((uint64_t)p[2] << 8) | p[3];
            hl = 4;
        } else if (len == 127) {
            if (s_rx_len - off < 10) break;
            len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | p[2 + i];
            hl = 10;
        }
        if (masked) hl += 4;
        if (len > RX_CAP - 16) {
            ws_close("frame too large");
            return;
        }
        if (s_rx_len - off < hl + len) break;
        uint8_t *payload = p + hl;
        if (masked) {
            for (uint64_t i = 0; i < len; i++) payload[i] ^= p[hl - 4 + (i & 3)];
        }
        switch (op) {
        case 0x1: on_text((const char *)payload, len); break;
        case 0x2:
            if (s_relay) relay_frame(payload, len);
            else on_binary(payload, len);
            break;
        case 0x8: ws_close("closed by bridge"); return;
        case 0x9: ws_frame(0xA, payload, len); break;
        default: break;
        }
        if (s_sock < 0) return;
        off += hl + len;
    }
    if (off) {
        memmove(s_rx, s_rx + off, s_rx_len - off);
        s_rx_len -= off;
    }
}

static void poll_socket(void)
{
    while (s_sock >= 0 && s_rx_len < RX_CAP) {
        int n = io_read(s_rx + s_rx_len, RX_CAP - s_rx_len);
        if (n > 0) {
            s_rx_len += n;
            parse_frames();
            continue;
        }
        if (n == 0) {
            ws_close("bridge closed");
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ws_close("recv failed");
        }
        break;
    }
}

/* ---- Commands ---- */

static bool ensure_connected(void)
{
    if (s_sock >= 0) return true;
    if (!dou_hatch_configured() || !dou_wifi_connected()) return false;
    return ws_connect();
}

static void pump_mic(bool all)
{
    static uint8_t buf[TX_CHUNK];
    while (s_sending && s_sock >= 0) {
        size_t avail = xStreamBufferBytesAvailable(s_in);
        if (!avail || (!all && avail < TX_CHUNK)) return;
        size_t n = xStreamBufferReceive(s_in, buf, avail < TX_CHUNK ? avail : TX_CHUNK, 0) & ~(size_t)1;
        if (n && !ws_send(0x2, buf, n)) return;
    }
}

static void handle_cmd(cmd_t *c)
{
    switch (c->type) {
    case CMD_BEGIN:
        if (s_turn && s_sock >= 0) send_simple("cancel", s_turn);
        reset_reply();
        s_text_turn = false;
        s_audio_turn = 0;
        s_turn = c->gen;
        if (!ensure_connected()) {
            emit(DOU_HATCH_EV_ERROR, c->gen, "Không kết nối được AICW");
            s_turn = 0;
            break;
        }
        {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "type", "turn_begin");
            cJSON_AddNumberToObject(o, "id", c->gen);
            cJSON_AddNumberToObject(o, "rate", RATE);
            s_sending = send_json(o);
        }
        break;
    case CMD_END:
        if (c->gen != s_turn || !s_sending) break;
        pump_mic(true);
        s_sending = false;
        if (send_simple("turn_end", s_turn)) {
            s_heard_by = now_us() + HEARD_TIMEOUT_US;
            s_done_by = now_us() + DONE_TIMEOUT_US;
        }
        break;
    case CMD_CANCEL:
        if (s_turn && c->gen == s_turn) {
            if (s_sock >= 0) send_simple("cancel", s_turn);
            s_turn = 0;
            s_sending = false;
            s_audio_turn = 0;
        }
        break;
    case CMD_TEXT:
        if (s_turn && s_sock >= 0) send_simple("cancel", s_turn);
        s_turn = atomic_fetch_add(&s_gen, 1) + 1;   /* its own id; a press replaces it */
        s_text_turn = true;
        if (!ensure_connected()) {
            dou_hatch_console("error", "AICW NOT CONNECTED", NULL);
            s_turn = 0;
        } else {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "type", "text_turn");
            cJSON_AddNumberToObject(o, "id", s_turn);
            cJSON_AddStringToObject(o, "text", c->text ? c->text : "");
            if (send_json(o)) dou_hatch_console("sent", NULL, "\"bytes\":%u", (unsigned)strlen(c->text ? c->text : ""));
            s_done_by = now_us() + DONE_TIMEOUT_US;
        }
        free(c->text);
        break;
    case CMD_TEXT_CANCEL:
        if (s_text_turn && s_turn) {
            send_simple("cancel", s_turn);
            s_turn = 0;
        }
        break;
    case CMD_CONNECT:
        if (s_sock < 0) {
            if (!ensure_connected() && dou_hatch_configured() && !dou_wifi_connected()) {
                dou_hatch_report(DOU_HATCH_OFFLINE, "Chờ Wi-Fi");
            }
        }
        break;
    case CMD_FORGET:
        if (s_sock >= 0) ws_close("settings changed");
        s_next_try = 0;
        break;
    case CMD_WAKE:
        break;
    }
}

static void session_task(void *arg)
{
    (void)arg;
    for (;;) {
        cmd_t c;
        TickType_t wait = (s_sending || s_turn) ? pdMS_TO_TICKS(10) : pdMS_TO_TICKS(50);
        if (xQueueReceive(s_cmds, &c, wait) == pdTRUE) {
            handle_cmd(&c);
            while (xQueueReceive(s_cmds, &c, 0) == pdTRUE) handle_cmd(&c);
        }
        int64_t t = now_us();
        /* Stay connected in the background so the first press is quick. */
        if (s_sock < 0 && dou_hatch_configured() && dou_wifi_connected() && t >= s_next_try) {
            if (!ws_connect()) s_next_try = t + (atomic_load(&s_resting) ? 4 * RETRY_US : RETRY_US);
        }
        if (s_sock < 0) continue;
        pump_mic(false);
        poll_socket();
        if (s_sock < 0) continue;
        if (s_turn && !s_text_turn && s_heard_by && t > s_heard_by) {
            emit(DOU_HATCH_EV_ERROR, s_turn, "AICW không nghe được");
            send_simple("cancel", s_turn);
            s_turn = 0;
            s_heard_by = s_done_by = 0;
        } else if (s_turn && s_done_by && t > s_done_by) {
            if (s_text_turn) dou_hatch_console("error", "timeout", NULL);
            else emit(DOU_HATCH_EV_ERROR, s_turn, "AICW trả lời quá lâu");
            send_simple("cancel", s_turn);
            s_turn = 0;
            s_heard_by = s_done_by = 0;
        }
        if (s_sock >= 0 && t - s_last_tx > PING_US) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "type", "ping");
            send_json(o);
        }
    }
}

/* ---- dou_chat.h ---- */

void dou_hatch_start(void)
{
    if (s_cmds) return;
    s_cmds = xQueueCreateWithCaps(16, sizeof(cmd_t), MALLOC_CAP_SPIRAM);
    s_events = xQueueCreateWithCaps(16, sizeof(ev_t), MALLOC_CAP_SPIRAM);
    s_in = xStreamBufferCreateWithCaps(IN_BYTES + 1, 1, MALLOC_CAP_SPIRAM);
    s_out = xStreamBufferCreateWithCaps(OUT_BYTES + 1, 1, MALLOC_CAP_SPIRAM);
    s_rx = heap_caps_malloc(RX_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_tx = heap_caps_malloc(TX_CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_reply = heap_caps_calloc(1, TEXT_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_enc = heap_caps_malloc(ENC_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_pt = heap_caps_malloc(ENC_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_dec = heap_caps_malloc(RX_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_cmds || !s_events || !s_in || !s_out || !s_rx || !s_tx || !s_reply || !s_enc || !s_pt || !s_dec ||
        xTaskCreatePinnedToCoreWithCaps(session_task, "aicw_chat", 20 * 1024, NULL, 5, NULL, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "start failed");
        return;
    }
    ESP_LOGI(TAG, "AICW voice session started");
}

void dou_hatch_chat_connect(void) { post(CMD_CONNECT, 0); }
void dou_hatch_chat_forget(void) { post(CMD_FORGET, 0); }

bool dou_hatch_ready(void)
{
    return s_cmds && dou_hatch_configured() && dou_wifi_connected();
}

#if CONFIG_DOU_RESEND_NOTES
dou_chat_fail_t dou_chat_turn_failure(void) { return DOU_CHAT_FAIL_NONE; }
#endif

void dou_hatch_turn_begin(void)
{
    uint32_t gen = atomic_fetch_add(&s_gen, 1) + 1;
    xStreamBufferReset(s_in);
    drain_out();
    post(CMD_BEGIN, gen);
}

void dou_hatch_turn_audio(const int16_t *pcm, size_t frames)
{
    size_t bytes = frames * sizeof(int16_t);
    if (xStreamBufferSend(s_in, pcm, bytes, 0) != bytes) {
        ESP_LOGW(TAG, "mic backlog full, dropped audio");
    }
}

size_t dou_hatch_turn_audio_wait(const int16_t *pcm, size_t frames, int wait_ms)
{
    return xStreamBufferSend(s_in, pcm, frames * sizeof(int16_t), pdMS_TO_TICKS(wait_ms)) / sizeof(int16_t);
}

void dou_hatch_turn_end(void) { post(CMD_END, atomic_load(&s_gen)); }

void dou_hatch_turn_cancel(void)
{
    uint32_t gen = atomic_fetch_add(&s_gen, 1);
    post(CMD_CANCEL, gen);
    drain_out();
}

void dou_hatch_set_resting(bool resting)
{
    if (atomic_exchange(&s_resting, resting) && !resting) post(CMD_WAKE, 0);
}

void dou_hatch_text_turn(char *text)
{
    cmd_t cmd = { CMD_TEXT, 0, text };
    if (!s_cmds || !dou_hatch_configured()) {
        dou_hatch_console("error", "AICW NOT SET UP", NULL);
        free(text);
    } else if (xQueueSend(s_cmds, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        dou_hatch_console("error", "BUSY", NULL);
        free(text);
    }
}

void dou_hatch_text_cancel(void) { post(CMD_TEXT_CANCEL, 0); }

#if CONFIG_DOU_PHOTO_NOTES
uint32_t dou_chat_note_id(void) { return atomic_load(&s_gen); }

bool dou_chat_attach_jpeg(char *jpeg_base64, size_t len, uint32_t note)
{
    (void)len;
    (void)note;
    free(jpeg_base64);   /* photos are not sent to AICW yet */
    return false;
}
#endif

bool dou_hatch_incoming(void) { return false; }

dou_hatch_ev_t dou_hatch_turn_event(char *text, size_t cap)
{
    static ev_t ev;   /* voice task only */
    while (s_events && xQueueReceive(s_events, &ev, 0) == pdTRUE) {
        if (ev.gen == atomic_load(&s_gen)) {
            strlcpy(text, ev.text, cap);
            return ev.type;
        }
    }
    return DOU_HATCH_EV_NONE;
}

static uint32_t reply_frames(size_t len)
{
    return s_reply_frames ? s_reply_frames : (uint32_t)((uint64_t)len * RATE / SPEECH_CHARS_PER_S);
}

float dou_turn_progress(size_t played, int *message)
{
    portENTER_CRITICAL(&s_lock);
    size_t len = s_reply ? strlen(s_reply) : 0;
    uint32_t frames = reply_frames(len);
    bool done = s_reply_audio_done;
    portEXIT_CRITICAL(&s_lock);
    if (message) *message = len ? 0 : -1;
    if (!len || !frames) return 0;
    if (done && played >= frames) return 1;
    float f = (float)played / frames;
    return f < 0.99f ? f : 0.99f;
}

bool dou_hatch_turn_caption(size_t played, char *out, size_t cap)
{
    static char copy[TEXT_MAX];   /* voice task only */
    portENTER_CRITICAL(&s_lock);
    if (s_reply) strlcpy(copy, s_reply, sizeof(copy));
    else copy[0] = '\0';
    uint32_t frames = reply_frames(strlen(copy));
    portEXIT_CRITICAL(&s_lock);
    size_t len = strlen(copy);
    if (!len) return false;
    size_t at = frames ? (size_t)((uint64_t)played * len / frames) : 0;
    if (at >= len) at = len - 1;
    return dou_hatch_caption_at(copy, at, out, cap);
}

size_t dou_hatch_turn_read(int16_t *pcm, size_t frames, int wait_ms)
{
    return xStreamBufferReceive(s_out, pcm, frames * sizeof(int16_t), pdMS_TO_TICKS(wait_ms)) / sizeof(int16_t);
}

size_t dou_hatch_mp3_selftest(int16_t **pcm)
{
    *pcm = NULL;
    return 0;
}
