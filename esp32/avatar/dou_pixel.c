// Copyright (c) Neurons AI. Licensed under the Apache License, Version 2.0.
/*
 * Dou, the AICW agent mascot, as the board avatar.
 *
 * Shape (docs/DOU-FACES.md): a round dark body (#2a2638)
 * with a thin glowing neon rim made of 22 small waves, two big spiral eyes
 * that turn slowly, no brows, looking straight ahead. The whole face tilts
 * +-7 degrees every 4.5 s, the body bounces, and it blinks every ~5.5 s.
 * Glow colour is the soft cream #fff4c2, tinted per state.
 *
 * Moods (same set as the AICW app): idle, happy, love, shy, surprised,
 * angry, sad, sleepy, sleep, dizzy, think, learn. They map onto the board
 * states: BOOT hatches out of an egg; IDLE is idle and now and then drifts
 * into a little mood of its own (shy, love, sleepy, a curious look);
 * LISTENING is surprised with rings and waves; THINKING is think with a "?";
 * working is learn (fast spirals, spinner); SPEAKING moves the mouth with
 * the voice and smiles in pauses; ERROR is dizzy with a red cross; OFF gets
 * sleepy and falls asleep with "z Z"; a pet is happy or love with hearts.
 *
 * Personality: a small curious pet that loves to learn, shy when praised.
 */

#include "dou_pixel.h"
#include "dou_speaking.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define W DOU_PX_W
#define H DOU_PX_H
#define TAU 6.2831853f

/* ---------------------------------------------------------------------------
 * Palette
 * ------------------------------------------------------------------------- */
enum {
    C_BG = 0,
    C_BODYD,     /* body, shadow side */
    C_BODY,      /* body #2a2638 */
    C_BODYL,     /* body, lit side */
    C_BODYH,     /* body highlight */
    C_RIM,       /* neon rim, live glow colour */
    C_RIMD,      /* rim falloff */
    C_RIMH,      /* rim hot core */
    C_EYE,       /* spiral strokes = glow */
    C_EYED,
    C_BLUSH,
    C_BLUSHD,
    C_MOUTH,
    C_TEAR,
    C_RED,
    C_G0,        /* state glow ramp, bright ... */
    C_G1,
    C_G2,
    C_G3,        /* ... deep */
    C_AURA1,
    C_AURA2,
    C_SPK,
    C_ACC,
    C_SHADOW,
    C_HEART,
    C_WHITE,
    C_GOLD,
    C_NIGHT,
    C_COUNT,
};

typedef struct {
    float r, g, b;
} rgb_t;

typedef struct {
    uint32_t f[4];
    uint32_t acc;
} scheme_t;

static const scheme_t SCHEMES[DOU_MODE_COUNT] = {
    [DOU_MODE_BOOT]      = { { 0xffffff, 0xfff4c2, 0xffd98a, 0xc99a3a }, 0xfff4c2 },
    [DOU_MODE_IDLE]      = { { 0xfffbe6, 0xfff4c2, 0xd9c47a, 0x7a6a3a }, 0xfff4c2 },
    [DOU_MODE_LISTENING] = { { 0xe8faff, 0x8fdcff, 0x3fa2ff, 0x2a5bd7 }, 0x8fd3ff },
    [DOU_MODE_THINKING]  = { { 0xf1edff, 0xc4b9ff, 0xa99bff, 0x5b3fd9 }, 0xa99bff },
    [DOU_MODE_SPEAKING]  = { { 0xeafff7, 0xa9f0da, 0x7ee0c3, 0x1f9a7a }, 0x7ee0c3 },
    [DOU_MODE_ERROR]     = { { 0xffe0e6, 0xff9a9a, 0xff6f6f, 0x8a2440 }, 0xff6f6f },
    [DOU_MODE_OFF]       = { { 0xe4e0ff, 0x9f96e0, 0x6a60c0, 0x2e2870 }, 0x8f86d9 },
};

/* A pet turns Dou pink; working turns it warm orange. */
static const scheme_t HAPPY_SCHEME = { { 0xfff0f6, 0xffc4dc, 0xff9ec4, 0xd9507f }, 0xff9ec4 };
static const scheme_t WORKING_SCHEME = { { 0xfff4dc, 0xffcf7a, 0xffa23f, 0xc9651f }, 0xffb54d };

static const uint32_t FIXED[C_COUNT] = {
    [C_BG] = 0x000000,
    [C_BODYD] = 0x1a1724,
    [C_BODY] = 0x2a2638,
    [C_BODYL] = 0x36314a,
    [C_BODYH] = 0x4a4462,
    [C_BLUSH] = 0xff8fb9,
    [C_BLUSHD] = 0xc8608c,
    [C_TEAR] = 0x8fd3ff,
    [C_RED] = 0xff6f6f,
    [C_SHADOW] = 0x14111c,
    [C_HEART] = 0xff5c93,
    [C_WHITE] = 0xffffff,
    [C_GOLD] = 0xffd36b,
    [C_NIGHT] = 0x0b0a1e,
};

static rgb_t s_scheme[5];
static bool s_scheme_init;
static uint32_t s_background;
void dou_pixel_set_background(uint32_t rgb) { s_background = rgb & 0xffffff; }
static uint16_t s_pal[C_COUNT];
static uint16_t s_pal_dim[C_COUNT];

static uint8_t s_fb[W * H];

static const uint8_t BAYER4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

static inline float bayer(int x, int y) { return (BAYER4[y & 3][x & 3] + 0.5f) / 16.0f; }
static inline rgb_t hex_rgb(uint32_t c) { return (rgb_t){ ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f }; }
static inline rgb_t mix(rgb_t a, rgb_t b, float t) { return (rgb_t){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
static inline rgb_t scale_rgb(rgb_t a, float k) { return (rgb_t){ a.r * k, a.g * k, a.b * k }; }
static inline uint16_t to565(rgb_t c)
{
    int r = (int)(c.r * 31 + 0.5f), g = (int)(c.g * 63 + 0.5f), b = (int)(c.b * 31 + 0.5f);
    r = r < 0 ? 0 : r > 31 ? 31 : r;
    g = g < 0 ? 0 : g > 63 ? 63 : g;
    b = b < 0 ? 0 : b > 31 ? 31 : b;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int iround(float v) { return (int)floorf(v + 0.5f); }

static uint32_t s_seed = 0x2468ace1u;
static float frand(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (s_seed >> 8) / 16777216.0f;
}

uint32_t dou_pixel_accent(dou_mode_t mode) { return SCHEMES[mode < DOU_MODE_COUNT ? mode : DOU_MODE_IDLE].acc; }
uint32_t dou_pixel_working_accent(void) { return WORKING_SCHEME.acc; }

static void update_palette(const scheme_t *target, float dt, float fade)
{
    float k = s_scheme_init ? 1.0f - expf(-dt * 7.0f) : 1.0f;
    for (int i = 0; i < 4; i++) s_scheme[i] = mix(s_scheme[i], hex_rgb(target->f[i]), k);
    s_scheme[4] = mix(s_scheme[4], hex_rgb(target->acc), k);
    s_scheme_init = true;

    rgb_t c[C_COUNT];
    for (int i = 0; i < C_COUNT; i++) c[i] = hex_rgb(FIXED[i]);
    c[C_BG] = hex_rgb(s_background);
    rgb_t glow = s_scheme[1];
    c[C_RIM] = scale_rgb(glow, 0.35f + 0.65f * fade);
    c[C_RIMH] = scale_rgb(mix(glow, hex_rgb(0xffffff), 0.55f), 0.35f + 0.65f * fade);
    c[C_RIMD] = mix(c[C_BODY], s_scheme[2], 0.45f * fade);
    c[C_EYE] = c[C_RIM];
    c[C_EYED] = mix(c[C_BODY], glow, 0.5f);
    c[C_MOUTH] = c[C_RIM];
    c[C_BODYL] = mix(c[C_BODYL], s_scheme[3], 0.12f);
    for (int i = 0; i < 4; i++) c[C_G0 + i] = scale_rgb(s_scheme[i], fade);
    c[C_AURA1] = mix(c[C_BG], s_scheme[3], 0.55f * fade);
    c[C_AURA2] = mix(c[C_BG], s_scheme[2], 0.35f * fade);
    c[C_SPK] = scale_rgb(mix(s_scheme[0], s_scheme[4], 0.3f), fade);
    c[C_ACC] = scale_rgb(s_scheme[4], fade);
    c[C_NIGHT] = mix(c[C_BG], c[C_NIGHT], 0.8f);
    for (int i = 0; i < C_COUNT; i++) {
        s_pal[i] = to565(c[i]);
        s_pal_dim[i] = to565(scale_rgb(c[i], 0.72f));
    }
}

/* ---------------------------------------------------------------------------
 * Drawing primitives (64 x 64, palette indices)
 * ------------------------------------------------------------------------- */
static inline void px(int x, int y, uint8_t c)
{
    if ((unsigned)x < W && (unsigned)y < H) s_fb[y * W + x] = c;
}

/* Thick line with round caps; dithered soft edge. */
static void line(float x0, float y0, float x1, float y1, float r, uint8_t c)
{
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    int xa = (int)floorf(fminf(x0, x1) - r - 1), xb = (int)ceilf(fmaxf(x0, x1) + r + 1);
    int ya = (int)floorf(fminf(y0, y1) - r - 1), yb = (int)ceilf(fmaxf(y0, y1) + r + 1);
    for (int y = ya; y <= yb; y++) {
        for (int x = xa; x <= xb; x++) {
            float fx = x + 0.5f - x0, fy = y + 0.5f - y0;
            float t = len2 > 0 ? clampf((fx * dx + fy * dy) / len2, 0, 1) : 0;
            float ex = fx - dx * t, ey = fy - dy * t;
            float d = sqrtf(ex * ex + ey * ey);
            if (d < r - 0.25f || (d < r + 0.35f && bayer(x, y) < (r + 0.35f - d) / 0.6f)) px(x, y, c);
        }
    }
}

static void disc(float cx, float cy, float r, uint8_t c)
{
    for (int y = (int)floorf(cy - r - 1); y <= (int)ceilf(cy + r + 1); y++)
        for (int x = (int)floorf(cx - r - 1); x <= (int)ceilf(cx + r + 1); x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            if (d < r - 0.25f || (d < r + 0.35f && bayer(x, y) < (r + 0.35f - d) / 0.6f)) px(x, y, c);
        }
}

static void ellipse(float cx, float cy, float rx, float ry, uint8_t c)
{
    for (int y = (int)floorf(cy - ry - 1); y <= (int)ceilf(cy + ry + 1); y++)
        for (int x = (int)floorf(cx - rx - 1); x <= (int)ceilf(cx + rx + 1); x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) px(x, y, c);
        }
}

/* Polyline of a parametric curve; stroke radius r. */
typedef struct {
    float cx, cy, cs, sn;   /* face transform: centre and tilt */
} xf_t;
static xf_t s_xf;

static inline void xf(float lx, float ly, float *ox, float *oy)
{
    *ox = s_xf.cx + lx * s_xf.cs - ly * s_xf.sn;
    *oy = s_xf.cy + lx * s_xf.sn + ly * s_xf.cs;
}

static void fline(float ax, float ay, float bx, float by, float r, uint8_t c)
{
    float x0, y0, x1, y1;
    xf(ax, ay, &x0, &y0);
    xf(bx, by, &x1, &y1);
    line(x0, y0, x1, y1, r, c);
}

/* Quadratic curve from a to b with control point q, in face coordinates. */
static void fquad(float ax, float ay, float qx, float qy, float bx, float by, float r, uint8_t c)
{
    float px0 = ax, py0 = ay;
    for (int i = 1; i <= 8; i++) {
        float t = i / 8.0f, u = 1 - t;
        float x = u * u * ax + 2 * u * t * qx + t * t * bx, y = u * u * ay + 2 * u * t * qy + t * t * by;
        fline(px0, py0, x, y, r, c);
        px0 = x;
        py0 = y;
    }
}

/* ---------------------------------------------------------------------------
 * State that lives across frames
 * ------------------------------------------------------------------------- */
typedef enum {
    MOOD_IDLE, MOOD_HAPPY, MOOD_LOVE, MOOD_SHY, MOOD_SURPRISED, MOOD_ANGRY,
    MOOD_SAD, MOOD_SLEEPY, MOOD_SLEEP, MOOD_DIZZY, MOOD_THINK, MOOD_LEARN,
    MOOD_TALK, MOOD_CURIOUS,
} mood_t;

static struct {
    float last_t;
    float blink_at, blink_t;
    bool dbl;
    float spin[2];          /* spiral phase of each eye */
    float idle_mood_at, idle_mood_until;
    mood_t idle_mood;
    float pet_last;
    mood_t pet_mood;
    float tilt;
} s;

static float blink_amount(float dt)
{
    if (s.blink_at <= 0) s.blink_at = 1.5f;
    s.blink_at -= dt;
    if (s.blink_at <= 0 && s.blink_t <= 0) {
        s.blink_t = 0.16f;
        s.dbl = frand() < 0.25f;
        s.blink_at = 2.2f + frand() * 3.3f;
    }
    if (s.blink_t > 0) {
        s.blink_t -= dt;
        float k = s.blink_t / 0.16f;
        float v = 1 - fabsf(k * 2 - 1);
        if (s.blink_t <= 0 && s.dbl) {
            s.dbl = false;
            s.blink_t = 0.16f;
        }
        return clampf(v * 1.6f, 0, 1);
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Body: dark round with wavy neon rim
 * ------------------------------------------------------------------------- */
static void draw_body(float cx, float cy, float R, float sx, float sy, float wave_ph, float rim_w)
{
    int x0 = (int)floorf(cx - R * sx - 3), x1 = (int)ceilf(cx + R * sx + 3);
    int y0 = (int)floorf(cy - R * sy - 3), y1 = (int)ceilf(cy + R * sy + 3);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (x + 0.5f - cx) / sx, dy = (y + 0.5f - cy) / sy;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > R + 3) continue;
            float a = atan2f(dy, dx);
            float rr = R + 0.6f * cosf(a * 18.0f + wave_ph);   /* scalloped neon edge */
            float e = d - rr;                                     /* <0 inside */
            if (e < -rim_w) {
                /* lit from the top left */
                float n = (-dx * 0.6f - dy * 0.8f) / (R > 0 ? R : 1);
                float v = n * 0.5f + 0.5f + (bayer(x, y) - 0.5f) * 0.12f;
                uint8_t c = v > 0.93f ? C_BODYL : v > 0.12f ? C_BODY : C_BODYD;
                px(x, y, c);
            } else if (e < 0) {
                float k = (e + rim_w) / rim_w;   /* 0 inner .. 1 outer */
                px(x, y, k > 0.35f && k < 0.8f ? C_RIMH : C_RIM);
            } else if (e < 0.9f) {
                px(x, y, C_RIMD);
            }
        }
    }
}

static void draw_egg(float cx, float cy, float h, float crack, float wob)
{
    for (int y = (int)(cy - h - 2); y <= (int)(cy + h + 2); y++) {
        for (int x = (int)(cx - h); x <= (int)(cx + h); x++) {
            float ly = (y + 0.5f - cy) / h;                /* -1 top .. 1 bottom */
            float half = 0.78f * h * sqrtf(fmaxf(0, 1 - ly * ly)) * (1 + 0.18f * ly);
            float lx = x + 0.5f - cx - wob * (ly - 1) * 1.5f;
            float e = fabsf(lx) - half;
            if (fabsf(ly) > 1) continue;
            if (e < -1.4f) {
                float v = (-lx / h * 0.6f - ly * 0.6f) * 0.5f + 0.5f + (bayer(x, y) - 0.5f) * 0.3f;
                px(x, y, v > 0.7f ? C_BODYL : v > 0.3f ? C_BODY : C_BODYD);
            } else if (e < 0) {
                px(x, y, C_RIM);
            }
        }
    }
    if (crack > 0) {
        float pts[][2] = { { -4, -6 }, { -1, -3 }, { -3, 0 }, { 1, 2 }, { -1, 5 }, { 3, 8 } };
        int n = 1 + (int)(crack * 5);
        for (int i = 0; i < n && i < 5; i++)
            line(cx + pts[i][0], cy + pts[i][1], cx + pts[i + 1][0], cy + pts[i + 1][1], 0.45f, C_RIMH);
    }
}

/* Spiral eye: 2.75 turns, tightening inwards. */
static void draw_spiral(float ex, float ey, float r, float phase, float squash, float width)
{
    float lx = 0, ly = 0;
    bool first = true;
    /* From the centre outwards, ~1.3 turns, ending in an open tail (like "@"). */
    for (float t = 0.6f; t <= 8.4f; t += 0.14f) {
        float rr = r * t / 8.4f;
        float x = ex + cosf(t + phase) * rr, y = ey + sinf(t + phase) * rr * squash;
        if (!first) fline(lx, ly, x, y, width, C_EYE);
        lx = x;
        ly = y;
        first = false;
    }
}

typedef enum { EYE_SPIRAL, EYE_SHUT, EYE_HAPPY, EYE_HALF, EYE_X, EYE_ANGRY, EYE_BIG } eye_t;

static void draw_eye(float ex, float ey, float r, eye_t style, float open, float phase, float dir)
{
    switch (style) {
    case EYE_HAPPY:   /* ^ */
        fquad(ex - r, ey + r * 0.3f, ex, ey - r * 1.1f, ex + r, ey + r * 0.3f, 0.75f, C_EYE);
        return;
    case EYE_SHUT:    /* closed, curved down */
        fquad(ex - r, ey, ex, ey + r * 0.6f, ex + r, ey, 0.7f, C_EYE);
        return;
    case EYE_X:
        fline(ex - r * 0.7f, ey - r * 0.7f, ex + r * 0.7f, ey + r * 0.7f, 0.75f, C_EYE);
        fline(ex - r * 0.7f, ey + r * 0.7f, ex + r * 0.7f, ey - r * 0.7f, 0.75f, C_EYE);
        return;
    default:
        break;
    }
    if (open < 0.18f) {
        fline(ex - r, ey + 0.5f, ex + r, ey + 0.5f, 0.6f, C_EYE);
        return;
    }
    float squash = open;
    if (style == EYE_HALF) squash *= 0.5f;
    if (style == EYE_ANGRY) squash *= 0.45f;
    float rr = style == EYE_BIG ? r * 1.25f : r;
    draw_spiral(ex, ey + (style == EYE_HALF ? r * 0.3f : 0), rr, phase * dir, squash, 0.7f);
    if (style == EYE_HALF) {   /* lid: a flat line on top */
        fline(ex - r - 0.5f, ey - r * 0.15f, ex + r + 0.5f, ey - r * 0.15f, 0.6f, C_EYE);
    }
    if (style == EYE_ANGRY) {   /* slanted lid towards the middle */
        float in = dir > 0 ? 1 : -1;
        fline(ex - r, ey - r * 0.6f - in * 1.5f, ex + r, ey - r * 0.6f + in * 1.5f, 0.6f, C_EYE);
    }
}

/* Mouths in face units (face spans about -12..12). */
typedef enum { M_NONE, M_SMILE, M_GRIN, M_SAD, M_FLAT, M_O, M_SHY, M_WAVY, M_TALK, M_SMALL } mouth_t;

static void draw_mouth(float mx, float my, mouth_t m, float open)
{
    switch (m) {
    case M_SMILE: fquad(mx - 4, my, mx, my + 3, mx + 4, my, 0.6f, C_MOUTH); break;
    case M_GRIN:
        fquad(mx - 5, my - 0.5f, mx, my + 4.5f, mx + 5, my - 0.5f, 0.7f, C_MOUTH);
        fline(mx - 4, my - 0.3f, mx + 4, my - 0.3f, 0.55f, C_MOUTH);
        break;
    case M_SAD: fquad(mx - 4, my + 2, mx, my - 1.5f, mx + 4, my + 2, 0.6f, C_MOUTH); break;
    case M_FLAT: fline(mx - 3.5f, my + 1, mx + 3.5f, my + 1, 0.6f, C_MOUTH); break;
    case M_SMALL: fline(mx - 1.6f, my + 1, mx + 1.6f, my + 1, 0.55f, C_MOUTH); break;
    case M_SHY: fquad(mx - 2, my, mx, my + 1.5f, mx + 2, my, 0.5f, C_MOUTH); break;
    case M_O: {
        float cx, cy;
        xf(mx, my + 1.5f, &cx, &cy);
        disc(cx, cy, 2.0f, C_MOUTH);
        disc(cx, cy, 0.9f, C_BODYD);
        break;
    }
    case M_WAVY:
        fquad(mx - 4.5f, my + 1, mx - 3.4f, my - 0.5f, mx - 2.2f, my + 1, 0.5f, C_MOUTH);
        fquad(mx - 2.2f, my + 1, mx - 1.1f, my + 2.5f, mx, my + 1, 0.5f, C_MOUTH);
        fquad(mx, my + 1, mx + 1.1f, my - 0.5f, mx + 2.2f, my + 1, 0.5f, C_MOUTH);
        fquad(mx + 2.2f, my + 1, mx + 3.4f, my + 2.5f, mx + 4.5f, my + 1, 0.5f, C_MOUTH);
        break;
    case M_TALK: {
        float h = 0.6f + open * 4.5f;
        float cx, cy;
        xf(mx, my + h * 0.5f, &cx, &cy);
        ellipse(cx, cy, 2.6f + open * 0.8f, h * 0.5f + 0.5f, C_MOUTH);
        if (h > 2.2f) ellipse(cx, cy + 0.4f, 1.6f, h * 0.5f - 0.9f, C_BLUSHD);
        break;
    }
    default: break;
    }
}

static void draw_blush(float x, float y, float strength)
{
    if (strength <= 0.02f) return;
    float cx, cy;
    xf(x, y, &cx, &cy);
    for (int yy = (int)cy - 2; yy <= (int)cy + 2; yy++)
        for (int xx = (int)cx - 4; xx <= (int)cx + 4; xx++) {
            float dx = (xx + 0.5f - cx) / 3.4f, dy = (yy + 0.5f - cy) / 1.6f, d = dx * dx + dy * dy;
            if (d > 1) continue;
            if (bayer(xx, yy) < strength * (1.15f - d)) px(xx, yy, d < 0.35f ? C_BLUSH : C_BLUSHD);
        }
}

/* ---------------------------------------------------------------------------
 * Effects
 * ------------------------------------------------------------------------- */
static void stamp(const char *const *rows, int n, int x0, int y0, uint8_t c, uint8_t alt)
{
    for (int y = 0; y < n; y++)
        for (int x = 0; rows[y][x]; x++) {
            char ch = rows[y][x];
            if (ch == '#') px(x0 + x, y0 + y, c);
            else if (ch == 'o') px(x0 + x, y0 + y, alt);
        }
}

static void draw_aura(float cx, float cy, float r, float strength)
{
    if (strength <= 0.01f) return;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float k = (d - (r - 9)) / 9;   /* 0 at the rim .. 1 at the edge of the glow */
            if (k < 0 || k > 1) continue;
            float v = (1 - k) * (1 - k) * strength;
            float th = bayer(x, y);
            if (v > th + 0.35f) px(x, y, C_AURA1);
            else if (v > th * 0.9f) px(x, y, C_AURA2);
        }
}

static void draw_rings(float cx, float cy, float t, float level, float speed, float R)
{
    for (int i = 0; i < 2; i++) {
        float ph = fmodf(t * speed + i * 0.5f, 1.0f);
        float r = R + 2 + ph * (10 + level * 6);
        int n = 28;
        for (int k = 0; k < n; k++) {
            if ((k & 1) && ph > 0.5f) continue;
            float a = k * TAU / n + t * 0.3f;
            px(iround(cx + cosf(a) * r), iround(cy + sinf(a) * r), ph < 0.6f ? C_G1 : C_G3);
        }
    }
}

static void draw_shadow(float cx, float y, float hw)
{
    for (int yy = iround(y) - 1; yy <= iround(y) + 1; yy++)
        for (int x = (int)(cx - hw); x <= (int)(cx + hw); x++) {
            float dx = (x + 0.5f - cx) / hw, dy = (yy + 0.5f - y) / 1.6f, d = dx * dx + dy * dy;
            if (d < 1 && bayer(x, yy) < (1 - d) * 0.9f) px(x, yy, C_SHADOW);
        }
}

static float s_spk_ph[8];
static void draw_sparkles(float cx, float cy, float R, float t, bool front, float speed, int count)
{
    static const char *const SPK[] = { ".#.", "###", ".#." };
    for (int i = 0; i < count && i < 8; i++) {
        if (s_spk_ph[i] == 0) s_spk_ph[i] = 0.3f + i * 0.77f;
        float a = t * speed * (0.5f + 0.1f * i) + s_spk_ph[i];
        bool is_front = sinf(a) > 0;
        if (is_front != front) continue;
        float r = R + 5 + (i % 3) * 2.5f;
        int x = iround(cx + cosf(a) * r), y = iround(cy + sinf(a) * r * 0.45f - 4 + (i % 2) * 6);
        float tw = sinf(t * 5 + i * 1.7f);
        if (tw > 0.2f) stamp(SPK, 3, x - 1, y - 1, C_SPK, C_SPK);
        else px(x, y, C_SPK);
    }
}

static void draw_heart(int x, int y, uint8_t c)
{
    static const char *const HEART[] = { ".#.#.", "#####", "#####", ".###.", "..#.." };
    stamp(HEART, 5, x - 2, y - 2, c, c);
}

static void draw_hearts(float cx, float top, float t, float amount)
{
    for (int i = 0; i < 3; i++) {
        float ph = fmodf(t * 0.7f + i * 0.33f, 1.0f);
        int x = iround(cx + (i - 1) * 13 + sinf(t * 3 + i) * 2), y = iround(top + 4 - ph * 16);
        if (ph < 0.85f * amount + 0.1f) draw_heart(x, y, C_HEART);
    }
}

static void draw_qmark(int x, int y, uint8_t c)
{
    static const char *const Q[] = { ".###.", "#...#", "...#.", "..#..", ".....", "..#.." };
    stamp(Q, 6, x - 2, y, c, c);
}

static void draw_bang(int x, int y)
{
    static const char *const B[] = { "##", "##", "##", "##", "..", "##" };
    stamp(B, 6, x, y, C_RED, C_RED);
}

static void draw_zz(float x, float y, float t)
{
    static const char *const ZS[] = { "###", "..#", ".#.", "#..", "###" };
    static const char *const ZB[] = { "####", "...#", "..#.", ".#..", "####" };
    float p = fmodf(t * 0.5f, 1.0f);
    stamp(ZS, 5, iround(x), iround(y - p * 4), C_G1, C_G1);
    float q = fmodf(t * 0.5f + 0.5f, 1.0f);
    stamp(ZB, 5, iround(x + 5), iround(y - 6 - q * 4), C_G0, C_G0);
}

static void draw_vein(int x, int y, float t)
{
    if (sinf(t * 9) < -0.2f) return;
    line(x, y, x + 3, y + 3, 0.5f, C_RED);
    line(x, y + 3, x + 3, y, 0.5f, C_RED);
    line(x + 4, y + 2, x + 7, y + 5, 0.5f, C_RED);
    line(x + 4, y + 5, x + 7, y + 2, 0.5f, C_RED);
}

static void draw_drop(float x, float y, uint8_t c)
{
    px(iround(x), iround(y), c);
    disc(x, y + 1.6f, 1.2f, c);
}

static void draw_spinner(float x, float y, float t)
{
    for (int i = 0; i < 6; i++) {
        float a = t * 7 + i * TAU / 6;
        px(iround(x + cosf(a) * 3), iround(y + sinf(a) * 3), i < 3 ? C_ACC : C_G3);
    }
}

static void draw_thought_dots(float x, float y, float t)
{
    for (int i = 0; i < 3; i++) {
        float on = fmodf(t * 1.6f - i * 0.33f, 1.0f);
        if (on < 0.6f) disc(x + i * 3.0f, y - i * 2.5f, 0.6f + i * 0.35f, C_G1);
    }
}

static void draw_crown(float cx, float top)
{
    line(cx - 6, top, cx - 4, top - 4, 0.6f, C_GOLD);
    line(cx - 4, top - 4, cx, top - 1, 0.6f, C_GOLD);
    line(cx, top - 1, cx + 4, top - 4, 0.6f, C_GOLD);
    line(cx + 4, top - 4, cx + 6, top, 0.6f, C_GOLD);
}

static void draw_night(float strength)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (s_fb[y * W + x] == C_BG && bayer(x, y) < strength * (0.4f + 0.6f * (1 - y / (float)H))) s_fb[y * W + x] = C_NIGHT;
    static const uint8_t ST[][2] = { { 6, 6 }, { 55, 9 }, { 12, 20 }, { 50, 24 }, { 8, 40 } };
    for (int i = 0; i < 5; i++) if (strength > 0.4f) px(ST[i][0], ST[i][1], C_SPK);
}

/* ---------------------------------------------------------------------------
 * Scale to screen (unchanged from the default renderer)
 * ------------------------------------------------------------------------- */
#define MAP_MAX 512
static uint8_t s_map[MAP_MAX];
static int s_size;

void dou_pixel_set_size(int px_)
{
    s_size = px_ < MAP_MAX ? px_ : MAP_MAX;
    bool grid = s_size >= 3 * W;
    for (int i = 0; i < s_size; i++) {
        int cell = i * W / s_size;
        bool edge = grid && (i + 1) * W / s_size != cell;
        s_map[i] = (uint8_t)(cell | (edge ? 0x80 : 0));
    }
}

void dou_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    int n = x1 - x0 + 1;
    const uint8_t *xmap = &s_map[x0];
    const uint16_t *prev = NULL;
    uint8_t prev_m = 0;
    for (int y = y0; y <= y1; y++, dst += stride_px) {
        uint8_t m = s_map[y];
        if (prev && m == prev_m) {
            memcpy(dst, prev, n * sizeof(uint16_t));
            continue;
        }
        const uint8_t *row = &s_fb[(m & 0x7f) * W];
        if (m & 0x80) {
            for (int i = 0; i < n; i++) dst[i] = s_pal_dim[row[xmap[i] & 0x7f]];
        } else {
            for (int i = 0; i < n; i++) {
                uint8_t xm = xmap[i];
                uint8_t c = row[xm & 0x7f];
                dst[i] = xm & 0x80 ? s_pal_dim[c] : s_pal[c];
            }
        }
        prev = dst;
        prev_m = m;
    }
}

/* ---------------------------------------------------------------------------
 * Frame
 * ------------------------------------------------------------------------- */
void dou_pixel_render(const dou_pose_t *p)
{
    static dou_speaking_t speech;
    float dt = s.last_t > 0 ? clampf(p->t - s.last_t, 0, 0.2f) : 0.04f;
    s.last_t = p->t;

    dou_mode_t mode = p->mode < DOU_MODE_COUNT ? p->mode : DOU_MODE_IDLE;
    float t = p->t, mt = p->mode_t, level = p->level, happy = p->happy;
    bool working = mode == DOU_MODE_THINKING && p->working > 0.5f;
    dou_speaking_update(&speech, mode == DOU_MODE_SPEAKING, dt, mt, level);

    /* ---- which mood ---- */
    mood_t mood = MOOD_IDLE;
    switch (mode) {
    case DOU_MODE_BOOT: mood = MOOD_IDLE; break;
    case DOU_MODE_LISTENING: mood = MOOD_SURPRISED; break;
    case DOU_MODE_THINKING: mood = working ? MOOD_LEARN : MOOD_THINK; break;
    case DOU_MODE_SPEAKING: mood = MOOD_TALK; break;
    case DOU_MODE_ERROR: mood = MOOD_DIZZY; break;
    case DOU_MODE_OFF: mood = mt < 0.7f ? MOOD_SLEEPY : MOOD_SLEEP; break;
    default: {
        /* Idle: now and then Dou shows a little mood of its own. */
        if (s.idle_mood_at <= 0) s.idle_mood_at = t + 6 + frand() * 6;
        if (mt < 0.5f) s.idle_mood_until = 0;
        if (t >= s.idle_mood_at && t > s.idle_mood_until) {
            static const mood_t PICK[] = { MOOD_SHY, MOOD_LOVE, MOOD_CURIOUS, MOOD_SLEEPY, MOOD_HAPPY, MOOD_SAD, MOOD_ANGRY };
            float r = frand();
            /* sulky and sad are rare */
            int i = r < 0.22f ? 0 : r < 0.42f ? 1 : r < 0.62f ? 2 : r < 0.78f ? 3 : r < 0.92f ? 4 : r < 0.97f ? 5 : 6;
            s.idle_mood = PICK[i];
            s.idle_mood_until = t + 3.2f + frand() * 1.5f;
            s.idle_mood_at = s.idle_mood_until + 7 + frand() * 9;
        }
        if (t < s.idle_mood_until) mood = s.idle_mood;
        /* Long idle: sleepy, then asleep. */
        if (mt > 240) mood = MOOD_SLEEP;
        else if (mt > 150) mood = MOOD_SLEEPY;
        break;
    }
    }
    if (happy > 0.02f && mode != DOU_MODE_ERROR && mode != DOU_MODE_OFF) {
        if (happy > s.pet_last + 0.3f || s.pet_last == 0) s.pet_mood = frand() < 0.5f ? MOOD_HAPPY : MOOD_LOVE;
        mood = s.pet_mood;
    }
    s.pet_last = happy;

    /* ---- colours ---- */
    float fade = mode == DOU_MODE_OFF ? clampf(1.0f - (mt - 0.6f) / 0.9f, 0.15f, 1) : 1.0f;
    const scheme_t *scheme = working ? &WORKING_SCHEME : &SCHEMES[mode];
    if (mood == MOOD_HAPPY || mood == MOOD_LOVE || mood == MOOD_SHY) scheme = &HAPPY_SCHEME;
    update_palette(scheme, dt, fade);
    float blink = blink_amount(dt);

    memset(s_fb, C_BG, sizeof(s_fb));

    /* ---- body motion ---- */
    float bob = 0, hop = 0, shake = 0, sx = 1, sy = 1, tilt_target = sinf(t * TAU / 4.5f) * 7.0f;
    float breathe = sinf(t * 2.0f + 1.0f) * 0.03f;
    switch (mood) {
    case MOOD_HAPPY: hop = fabsf(sinf(t * TAU / 0.65f)) * 3.5f; tilt_target *= 0.5f; break;
    case MOOD_LOVE: tilt_target = sinf(t * 3.0f) * 10; bob = sinf(t * 6) * 0.6f; break;
    case MOOD_SHY: tilt_target = -9; bob = 1.0f; break;
    case MOOD_SURPRISED: bob = mt < 0.25f ? -2.5f * (1 - mt / 0.25f) : sinf(t * 3) * 0.5f; tilt_target = 0; break;
    case MOOD_ANGRY: shake = sinf(t * 40) * 0.8f; tilt_target = 0; break;
    case MOOD_SAD: bob = 1.5f + sinf(t * 1.1f) * 0.6f; tilt_target = -4; breathe *= 1.6f; break;
    case MOOD_SLEEPY: tilt_target = sinf(t * 0.9f) * 14; bob = 1.0f; break;
    case MOOD_SLEEP: tilt_target = 9; bob = 1.5f + sinf(t * 1.2f) * 0.8f; breathe = sinf(t * 1.2f) * 0.05f; break;
    case MOOD_DIZZY: tilt_target = sinf(t * 5) * 12; shake = mt < 0.6f ? sinf(t * 30) * 1.2f : 0; break;
    case MOOD_THINK: tilt_target = 10 + sinf(t * 1.3f) * 3; bob = sinf(t * 2.4f) * 0.7f; break;
    case MOOD_LEARN: bob = sinf(t * 4.2f) * 0.5f; tilt_target = sinf(t * 2) * 3; break;
    case MOOD_TALK:
        bob = sinf(t * 2.5f) * 0.25f - speech.energy * 0.8f + speech.nod * 0.8f;
        tilt_target = (speech.lift[1] - speech.lift[0]) * 8 + sinf(t * TAU / 4.5f) * 4;
        break;
    case MOOD_CURIOUS: tilt_target = 13; bob = sinf(t * 2) * 0.5f; break;
    default: bob = sinf(t * 1.8f) * 1.0f; break;
    }
    s.tilt += (tilt_target - s.tilt) * (1 - expf(-dt * 6));

    /* Boot: an egg wobbles, cracks and Dou pops out. */
    if (mode == DOU_MODE_BOOT && mt < 1.1f) {
        float wob = mt < 0.9f ? sinf(mt * 28) * (0.4f + mt) : 0;
        draw_aura(32, 34, 26, 0.5f * clampf(mt / 0.6f, 0, 1));
        draw_shadow(32, 56.5f, 11);
        draw_egg(32, 35, 17, clampf((mt - 0.25f) / 0.6f, 0, 1), wob);
        return;
    }
    float pop = mode == DOU_MODE_BOOT ? clampf((mt - 1.1f) / 0.45f, 0, 1) : 1.0f;
    float squash = 1.0f - (1.0f - pop) * 0.35f + sinf(pop * 3.1416f) * 0.08f;

    float R = 19.5f;
    sx = (1 + breathe + level * 0.03f) * (2.0f - squash);
    sy = (1 - breathe) * squash;
    if (mood == MOOD_SURPRISED && mt < 0.3f) sy *= 1.06f;
    float cx = 32.0f + shake;
    float cy = 56.0f - R * sy - 1 + bob * 0.6f - hop;

    /* ---- background ---- */
    bool night = mood == MOOD_SLEEP;
    float aura_s = (0.42f + level * 0.4f) * fade * (night ? 0.5f : 1.0f);
    draw_aura(cx, cy, R + 9 + sinf(t * 1.5f), aura_s);
    if (night) draw_night(clampf(mode == DOU_MODE_OFF ? (mt - 0.7f) / 0.5f : 1, 0, 1));
    if (mode == DOU_MODE_LISTENING) draw_rings(cx, cy, t, level, 0.9f, R);
    else if (mode == DOU_MODE_SPEAKING) draw_rings(cx, cy, t, level, 0.6f, R);
    draw_shadow(32, 58.5f, 13.0f - hop * 0.8f);

    float spk_speed = working ? 1.8f : mood == MOOD_THINK ? 2.8f : mode == DOU_MODE_LISTENING ? 1.2f : 0.7f;
    int spk_count = night ? 0 : mode == DOU_MODE_BOOT ? (int)(pop * 6) : working ? 3 : (int)(5 * fade);
    draw_sparkles(cx, cy, R, t, false, spk_speed, spk_count);

    /* ---- body ---- */
    draw_body(cx, cy, R, sx, sy, t * 0.6f, 1.9f);

    /* ---- face ---- */
    float tilt = s.tilt * 3.1416f / 180.0f;
    s_xf.cx = cx;
    s_xf.cy = cy;
    s_xf.cs = cosf(tilt);
    s_xf.sn = sinf(tilt);

    float spin_speed = 0.9f;
    float dir2 = 1;
    eye_t eye = EYE_SPIRAL;
    mouth_t mouth = M_NONE;
    float open = 1 - blink, mouth_open = 0, blush = 0;
    float face_dy = 0, eye_r = 5.4f;

    switch (mood) {
    case MOOD_HAPPY: eye = EYE_HAPPY; mouth = M_GRIN; blush = 1; break;
    case MOOD_LOVE: spin_speed = 3.2f; eye_r *= 1 + 0.08f * sinf(t * 9); mouth = M_SMILE; blush = 1; break;
    case MOOD_SHY: face_dy = 1.5f; mouth = M_SHY; blush = 1; break;
    case MOOD_SURPRISED: eye = EYE_BIG; spin_speed = 0; mouth = M_O; break;
    case MOOD_ANGRY: eye = EYE_ANGRY; spin_speed = -1.5f; mouth = M_FLAT; break;
    case MOOD_SAD: spin_speed = 0.35f; face_dy = 1.5f; mouth = M_SAD; break;
    case MOOD_SLEEPY: eye = EYE_HALF; spin_speed = 0.3f; break;
    case MOOD_SLEEP: eye = EYE_SHUT; mouth = M_SMALL; break;
    case MOOD_DIZZY: spin_speed = 5.5f; dir2 = -1; mouth = M_WAVY; break;
    case MOOD_THINK: mouth = M_SMALL; break;
    case MOOD_LEARN: spin_speed = 4.0f; mouth = M_SMILE; face_dy = 0.8f; break;
    case MOOD_CURIOUS: eye_r *= 1.08f; mouth = M_SMALL; break;
    case MOOD_TALK:
        mouth = M_TALK;
        mouth_open = clampf(speech.mouth * (speech.heard_voice ? 0.9f : 0.6f), 0, 1);
        blush = 0.35f;
        if (speech.heard_voice && speech.quiet > 0.52f) {
            eye = EYE_HAPPY;
            mouth = M_SMILE;
        } else if (speech.eye_wide > 0.4f) {
            eye = EYE_BIG;
        }
        break;
    default: break;
    }
    if (mode == DOU_MODE_BOOT) open *= clampf((mt - 1.35f) / 0.3f, 0, 1);
    if (mode == DOU_MODE_OFF && mood == MOOD_SLEEPY) open *= clampf(1 - mt / 0.7f, 0, 1);

    s.spin[0] += dt * spin_speed * 2.2f;
    s.spin[1] += dt * spin_speed * 2.2f;
    float ey = -3.5f + face_dy, ex = 6.2f;
    draw_eye(-ex, ey, eye_r, eye, open, s.spin[0], 1);
    draw_eye(ex, ey, eye_r, eye, open, s.spin[1], dir2);

    draw_blush(-ex - 3.5f, ey + 6.5f, blush);
    draw_blush(ex + 3.5f, ey + 6.5f, blush);
    draw_mouth(0, ey + 7.5f, mouth, mouth_open);

    /* ---- foreground effects ---- */
    float top = cy - R * sy;
    draw_sparkles(cx, cy, R, t, true, spk_speed, spk_count);
    switch (mood) {
    case MOOD_HAPPY:
    case MOOD_LOVE: draw_hearts(cx, top, t, mood == MOOD_LOVE ? 1 : fmaxf(happy, 0.6f)); break;
    case MOOD_SHY: draw_drop(cx + 15, top + 3 + fmodf(t * 3, 1) * 4, C_TEAR); break;
    case MOOD_SAD: {
        float tx, ty;
        xf(-ex - 1, ey + eye_r + 1 + fmodf(t * 1.2f, 1) * 6, &tx, &ty);
        draw_drop(tx, ty, C_TEAR);
        break;
    }
    case MOOD_ANGRY: draw_vein(iround(cx + 11), iround(top + 1), t); break;
    case MOOD_SLEEP: draw_zz(cx + 13, top + 2, t); break;
    case MOOD_THINK: draw_qmark(iround(cx + 17), iround(top - 4 + sinf(t * 4) * 1.2f), C_G1); draw_thought_dots(cx + 12, top + 2, t); break;
    case MOOD_LEARN: draw_spinner(cx + 17, top + 1, t); break;
    case MOOD_CURIOUS: draw_qmark(iround(cx + 17), iround(top - 3), C_G1); break;
    case MOOD_DIZZY:
        if (mode == DOU_MODE_ERROR) draw_bang(iround(cx + 18), iround(top - 3));
        for (int i = 0; i < 3; i++) {
            float a = t * 4 + i * TAU / 3;
            px(iround(cx + cosf(a) * 9), iround(top - 2 + sinf(a) * 2), C_SPK);
        }
        break;
    case MOOD_SURPRISED:
        if (mode == DOU_MODE_LISTENING) {
            /* sound waves either side, growing with the voice */
            for (int side = -1; side <= 1; side += 2)
                for (int k = 0; k < 2; k++) {
                    float r = R + 3 + k * 3 + level * 3;
                    for (float a = -0.5f; a <= 0.5f; a += 0.12f)
                        px(iround(cx + side * cosf(a) * r), iround(cy + sinf(a) * r), k ? C_G2 : C_G1);
                }
        }
        break;
    default: break;
    }
    if (mode == DOU_MODE_BOOT && pop >= 1 && mt < 2.6f) draw_crown(cx, top - 1 - (2.6f - mt) * 3);
    (void)draw_heart;
}
