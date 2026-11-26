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

/*
 * The video decoders behind dou_video.c, one interface: H.264 (tinyh264)
 * and, with CONFIG_DOU_VIDEO_VP8, VP8 (libvpx_vp8). A decoder
 * reports the rows of its picture that are final as it decodes them, so the
 * panel can start before the frame is done.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A decoded 4:2:0 picture: three planes with their own strides. w and h are
 * the decoder's (macroblock-aligned) size. */
typedef struct {
    const uint8_t *y, *u, *v;
    int y_stride, c_stride;
    int w, h;
} dou_pic_t;

typedef struct dou_vdec dou_vdec_t;

/* pic: the picture so far; rows: its luma rows that are final. */
typedef void (*dou_vdec_progress_t)(void *ctx, const dou_pic_t *pic, int rows);

/* codec: DOU_VIDEO_CODEC_H264 or DOU_VIDEO_CODEC_VP8 (dou_vstream.h). NULL: no memory
 * or a codec this build doesn't have. */
/* A decoder for codec whose pictures are at most max_w x max_h (the
 * stream's negotiated size): a stream that describes a larger one is an
 * error, before anything is allocated for it. */
dou_vdec_t *dou_vdec_open(uint8_t codec, int max_w, int max_h);
/* True if the access unit may make the decoder reallocate its pictures (an
 * H.264 parameter set or IDR slice, with or without a start code before it;
 * a VP8 keyframe): pictures it handed out before may be freed by
 * dou_vdec_decode, so wait for their readers first. */
bool dou_vdec_may_realloc(const dou_vdec_t *d, const uint8_t *buf, uint32_t len);
/* Optional H.264 reference-frame limit; no effect on VP8. Set before decode. */
void dou_vdec_limit_refs(dou_vdec_t *d, unsigned max_refs);
void dou_vdec_close(dou_vdec_t *d);
bool dou_vdec_has(uint8_t codec);
void dou_vdec_set_progress(dou_vdec_t *d, dou_vdec_progress_t cb, void *ctx);

typedef enum {
    DOU_VDEC_PICTURE = 1,     /* *pic holds a picture to show */
    DOU_VDEC_NO_PICTURE = 0,  /* decoded, nothing to show (parameter sets, a hidden frame) */
    DOU_VDEC_ERROR = -1,      /* not decodable: wait for a keyframe */
} dou_vdec_result_t;

/* One frame: an H.264 access unit or a VP8 frame. The picture stays valid
 * until the decode after next (the decoder predicts the next frame from it). */
dou_vdec_result_t dou_vdec_decode(dou_vdec_t *d, const uint8_t *buf, uint32_t len, dou_pic_t *pic);

#ifdef __cplusplus
}
#endif
