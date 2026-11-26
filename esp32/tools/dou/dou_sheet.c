/* Dou contact sheet: renders each board state (and idle long enough to show
 * its own moods) and writes one PPM grid with 4 frames per row.
 *   cc -O2 -I components/dou tools/dou/dou_sheet.c components/dou/avatar/dou_pixel.c -lm -o /tmp/dou_sheet && /tmp/dou_sheet out.ppm
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dou_pixel.h"

#define S 3
#define N (DOU_PX_W * S)
#define COLS 4
#define ROWS 12
static uint16_t cell[N * N];
static uint8_t sheet[ROWS * N][COLS * N][3];

typedef struct { dou_mode_t mode; float mode_t0; float happy; float working; float level; float idle_skip; } row_t;

static void put(int r, int c)
{
    dou_pixel_scale(cell, N, 0, N - 1, 0, N - 1);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            uint16_t v = cell[y * N + x];
            uint8_t *p = sheet[r * N + y][c * N + x];
            p[0] = ((v >> 11) & 31) * 255 / 31;
            p[1] = ((v >> 5) & 63) * 255 / 63;
            p[2] = (v & 31) * 255 / 31;
        }
}

int main(int argc, char **argv)
{
    dou_pixel_set_size(N);
    const row_t rows[ROWS] = {
        { DOU_MODE_BOOT, 0, 0, 0, 0, 0 },
        { DOU_MODE_IDLE, 5, 0, 0, 0, 0 },
        { DOU_MODE_LISTENING, 0, 0, 0, .6f, 0 },
        { DOU_MODE_THINKING, 0, 0, 0, 0, 0 },
        { DOU_MODE_THINKING, 0, 0, 1, 0, 0 },
        { DOU_MODE_SPEAKING, 0, 0, 0, .7f, 0 },
        { DOU_MODE_IDLE, 5, 1, 0, 0, 0 },
        { DOU_MODE_ERROR, 0, 0, 0, 0, 0 },
        { DOU_MODE_OFF, 0, 0, 0, 0, 0 },
        { DOU_MODE_IDLE, 160, 0, 0, 0, 0 },   /* long idle: sleepy */
        { DOU_MODE_IDLE, 250, 0, 0, 0, 0 },   /* asleep */
        { DOU_MODE_IDLE, 30, 0, 0, 0, 1 },    /* idle moods sampled over time */
    };
    float base = 10;
    for (int r = 0; r < ROWS; r++) {
        const row_t *w = &rows[r];
        int shot = 0;
        float span = w->mode == DOU_MODE_BOOT ? 2.4f : w->mode == DOU_MODE_OFF ? 1.6f : 2.0f;
        if (w->idle_skip) span = 120;
        for (float tt = -1.0f; shot < COLS; tt += 0.04f) {
            dou_pose_t p = { w->mode, base + tt, w->mode_t0 + (tt < 0 ? 0 : tt), w->level ? w->level * (0.5f + 0.5f * fabsf(sinf(tt * 7))) : 0,
                              w->happy, w->working };
            if (w->mode_t0 == 0 && tt < 0) p.mode_t = 0;
            dou_pixel_render(&p);
            float at = (shot + 0.5f) * span / COLS;
            if (tt >= at) { put(r, shot); shot++; }
        }
        base += 1000;
    }
    FILE *f = fopen(argc > 1 ? argv[1] : "dou_sheet.ppm", "wb");
    fprintf(f, "P6 %d %d 255\n", COLS * N, ROWS * N);
    fwrite(sheet, 1, sizeof(sheet), f);
    fclose(f);
    return 0;
}
