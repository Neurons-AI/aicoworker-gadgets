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

/* Between the Dou session (dou_chat_session.cpp) and the realtime call
 * running over it (dou_voice_call.cpp). Session task only, except where noted. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- dou_chat_session.cpp, for the call ---- */

/* The most body one inbound frame can carry (the session's reassembly scratch). */
#define DOU_SESSION_FRAME_MAX (64 * 1024)

/* Opens a POST stream: the media (`media`) or an end-call. Returns its id, or
 * 0 if the connection failed. */
int64_t dou_session_call_open(bool media, const char *path, const char *content_type, const char *body, bool end_body);
bool dou_session_call_send(int64_t id, const uint8_t *data, size_t len, bool end_body);
void dou_session_call_reset(int64_t id);
/* Connects if need be (blocking); false if Dou can't be reached. */
bool dou_session_call_connect(void);
/* Any task: wakes the session task to look at the call's commands. */
void dou_session_call_wake(void);

/* ---- dou_voice_call.cpp, for the session ---- */

/* From dou_hatch_start(): queues and buffers. */
bool dou_voice_call_init(void);
/* Each pass of the session loop, connected or not. False: the connection has
 * to go, because a send failed or Dou didn't confirm an end-call (dropping
 * the connection is what then ends the call on Dou's side). */
bool dou_voice_call_poll(void);
/* A call is starting, running or ending: poll quickly, don't idle-close. */
bool dou_voice_call_busy(void);
/* The reply buffer can't take another frame's audio: leave the socket for now. */
bool dou_voice_call_rx_full(void);
/* Frames on the call's streams. `date` is the response's Date header, or NULL. */
void dou_voice_call_head(int64_t id, int status, const char *date, size_t date_len);
void dou_voice_call_data(int64_t id, const uint8_t *data, size_t len);
void dou_voice_call_stream_end(int64_t id, bool ok, int status);
/* The connection is gone (from disconnect()). */
void dou_voice_call_disconnected(void);

#ifdef __cplusplus
}
#endif
