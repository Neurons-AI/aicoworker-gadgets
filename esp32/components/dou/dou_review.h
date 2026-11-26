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

#ifdef __cplusplus
extern "C" {
#endif

#define DOU_REVIEW_CAP (32 * 1024)

/* A bounded RAM copy survives the chat turn ending. */
esp_err_t dou_review_init(void);
/* A new identity for each chat turn, typed or spoken. Replies stored under
 * one identity are joined; the next identity's first reply replaces them. */
uint32_t dou_review_turn(void);
void dou_review_store(uint32_t turn, const char *text);
/* Report unavailable capture without discarding an earlier answer. */
void dou_review_unavailable(void);
/* As store, with truncation already detected by the receiver. */
void dou_review_store_complete(uint32_t turn, const char *text, bool shortened);
typedef struct {
    uint32_t revision; /* content only: Watcher paging never changes it */
    bool available, failed, shortened;
} dou_review_snapshot_t;
/* Copy a stable snapshot without opening or changing Watcher review.
 * out may be NULL to poll metadata. Call from tasks, never interrupt handlers. */
void dou_review_snapshot(char *out, size_t cap, dou_review_snapshot_t *snapshot);
bool dou_review_step(int direction);
void dou_review_close(void);
bool dou_review_active(void);
/* Copies the selected page and returns its one-based position and total. */
bool dou_review_page(char *out, size_t cap, int *page, int *pages);

/* The screen's copy of the review page, apart from the live caption. */
typedef struct {
    uint32_t version;
    bool shown;
    bool stale;   /* set when the page layout changes: the page is wrapped again */
    int page, pages;
} dou_review_view_t;

/* While reviewing, true, with the page in out. *changed says whether the
 * label needs redrawing: a new page, or review just ended and the live
 * caption has to come back. out is only written when the page changes. */
bool dou_review_frame(dou_review_view_t *view, char *out, size_t cap, bool *changed);

#ifdef __cplusplus
}
#endif
