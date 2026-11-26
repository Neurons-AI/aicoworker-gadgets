/* Copyright (c) Meta Platforms, Inc. and affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once
#include "sdkconfig.h"
#include "dou_avatar_render.h"

/* Backend owns immutable pixels. Every acquire, including a decoder's open,
 * must be paired with release after the last strip read. */
typedef struct dou_avatar_image {
    dou_avatar_pixels_t data;
    uint32_t revision;
#if CONFIG_DOU_AVATAR_ANIMATIONS
    void (*release)(const struct dou_avatar_image *image);
#endif
} dou_avatar_image_t;

void dou_avatar_start(void);
dou_avatar_source_t dou_avatar_source(void);
void dou_avatar_select(dou_avatar_source_t source);
void dou_avatar_refresh(void);
void dou_avatar_clear(void);
const char *dou_avatar_status(void);
const dou_avatar_image_t *dou_avatar_acquire(void);
void dou_avatar_release(const dou_avatar_image_t *image);
void dou_avatar_pairing_changed(void);
/* Changes when saved content is cleared or its account/VM binding changes.
 * Source selection alone does not invalidate content-derived metadata. Zero
 * means no image from the current context has been published yet. */
uint32_t dou_avatar_content_epoch(void);
/* Also used by Apps when avatar support is compiled in: one shared mount. */
bool dou_avatar_files_mount(void);

#if CONFIG_DOU_AVATAR_ANIMATIONS
bool dou_avatar_files_space(size_t needed);
/* Pairing changes hide all personal surfaces immediately, before worker cleanup. */
bool dou_avatar_personal_allowed(void);
#endif
