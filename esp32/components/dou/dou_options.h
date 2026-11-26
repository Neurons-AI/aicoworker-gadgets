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

#ifdef __cplusplus
extern "C" {
#endif

/* One retained text question. Oversized widgets are rejected, never truncated. */
#define DOU_OPTIONS_MAX 8
#define DOU_OPTION_BYTES 192
#define DOU_OPTION_ID_BYTES 80
#define DOU_OPTION_QUESTION_BYTES 384
typedef enum {
    DOU_OPTIONS_EMPTY, DOU_OPTIONS_READY, DOU_OPTIONS_QUEUED,
    DOU_OPTIONS_SENDING, DOU_OPTIONS_SELECTED, DOU_OPTIONS_UNCERTAIN,
    DOU_OPTIONS_STALE,
} dou_options_status_t;

typedef struct {
    uint32_t generation, revision;
    char widget_id[DOU_OPTION_ID_BYTES], message_id[DOU_OPTION_ID_BYTES];
    char question[DOU_OPTION_QUESTION_BYTES];
    char labels[DOU_OPTIONS_MAX][DOU_OPTION_BYTES];
    int count, selected;
    double state_version;
    dou_options_status_t status;
    bool saved;
    char detail[96];
} dou_options_t;

/* Snapshots and commands can cross tasks. No caller retains internal pointers. */
bool dou_options_snapshot(dou_options_t *out);
void dou_options_clear(void);
void dou_options_disconnected(void);
bool dou_options_publish(const dou_options_t *question);
bool dou_options_request(uint32_t generation, int index);
bool dou_options_pending(void);
bool dou_options_waiting(void);
bool dou_options_take(dou_options_t *out);
/* USB console: options, or options.select=GENERATION,INDEX (zero based). */
bool dou_options_console(const char *line);
void dou_options_result(uint32_t generation, dou_options_status_t status,
                         bool saved, const char *detail);

/* Session-task JSON adapter; not linked into Link-only or simulator builds. */
struct cJSON;
bool dou_options_receive(const struct cJSON *payload);
/* message_id: the row's id when the event carries it outside the payload (or NULL).
 * streamed_text must belong to this payload's message, never a previous reply. */
bool dou_options_receive_message(const struct cJSON *payload, const char *message_id,
                                  const char *streamed_text);
struct cJSON *dou_options_chat_body(const dou_options_t *q, const char *interaction_id);
struct cJSON *dou_options_state_body(const dou_options_t *q, const char *mutation_id);
bool dou_options_state_response(const dou_options_t *q, const struct cJSON *response);

#ifdef __cplusplus
}
#endif
