// Copyright (c) Neurons AI. Licensed under the Apache License, Version 2.0.
/* Renders Dou's happy reaction from avatar/dou_pixel.c as raw frames for
 * tools/dou/make_dou_gif.py, which writes avatar/dou_happy.gif (the pixel-art
 * animation the small SPI-display boards play; see tools/gen_happy_anim.py).
 *   cc -O2 -I components/dou tools/dou/dou_gif.c avatar/dou_pixel.c -lm -o /tmp/dou_gif && /tmp/dou_gif > /tmp/dou.raw
 * Output: 75 frames of 64x64 RGB565 (little endian), 40 ms apart.
 */
#include <math.h>
#include <stdio.h>
#include "dou_pixel.h"

int main(void)
{
    static uint16_t buf[DOU_PX_W * DOU_PX_H];
    dou_pixel_set_size(DOU_PX_W);
    float t0 = 20;
    for (int i = -40; i < 75; i++) {
        float t = i * 0.04f;
        float happy = t < 0 ? 0 : 1;
        dou_pose_t p = { DOU_MODE_IDLE, t0 + t, 5 + (t < 0 ? 0 : t), 0, happy, 0 };
        dou_pixel_render(&p);
        if (i < 0) continue;
        dou_pixel_scale(buf, DOU_PX_W, 0, DOU_PX_W - 1, 0, DOU_PX_H - 1);
        fwrite(buf, 2, DOU_PX_W * DOU_PX_H, stdout);
    }
    return 0;
}
