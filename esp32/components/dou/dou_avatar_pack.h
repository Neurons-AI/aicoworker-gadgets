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
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DOU_AVATAR_PACK_MAX = 1024 * 1024, DOU_AVATAR_FRAME_MAX = 32768,
       DOU_AVATAR_CLIPS = 6, DOU_AVATAR_PACK_HEADER = 64 };
typedef enum { DOU_CLIP_IDLE, DOU_CLIP_WAKING, DOU_CLIP_WORKING,
    DOU_CLIP_MAKING, DOU_CLIP_WAITING, DOU_CLIP_CELEBRATE } dou_avatar_clip_kind_t;
typedef struct { uint32_t offset, bytes, frames; bool loop; } dou_avatar_clip_t;
typedef struct {
    const uint8_t *data;
    size_t size;
    uint16_t width, height, fps;
    uint32_t coverage;
    dou_avatar_clip_t clip[DOU_AVATAR_CLIPS];
} dou_avatar_pack_t;
/* The caller owns immutable data. A complete bounded validation, including
 * every frame CRC and exact EOF, precedes publication or decoder input. */
bool dou_avatar_pack_parse(const uint8_t *data, size_t size, const uint8_t source[32],
                            dou_avatar_pack_t *out);
uint32_t dou_avatar_pack_crc(const void *data, size_t size);
bool dou_avatar_pack_frame(const dou_avatar_pack_t *pack, unsigned kind, uint32_t *cursor,
                            const uint8_t **data, uint32_t *size);
