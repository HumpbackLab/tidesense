#include "lk_shared.h"
#include <math.h>
#include <stddef.h>

static const unsigned int widths[LK_PROBE_LEVELS] = {640, 320, 160, 80, 40};
static const unsigned int heights[LK_PROBE_LEVELS] = {400, 200, 100, 50, 25};
static const unsigned int offsets[LK_PROBE_LEVELS] = {0, 256000, 320000, 336000, 340000};

static float sample(const uint8_t *im, int w, int h, float x, float y)
{
    int ix = (int)x;
    int iy = (int)y;
    float fx = x - ix;
    float fy = y - iy;
    const uint8_t *p;
    if (x < 0.0f) { ix = 0; fx = 0.0f; }
    if (y < 0.0f) { iy = 0; fy = 0.0f; }
    if (ix + 1 >= w) { ix = w - 2; fx = 1.0f; }
    if (iy + 1 >= h) { iy = h - 2; fy = 1.0f; }
    p = im + iy * w + ix;
    return (1.0f - fy) * ((1.0f - fx) * p[0] + fx * p[1]) +
           fy * ((1.0f - fx) * p[w] + fx * p[w + 1]);
}

static void make_pyramid(const uint8_t *src, uint8_t *pyr)
{
    unsigned int l, x, y;
    for (y = 0; y < LK_PROBE_HEIGHT; ++y)
        for (x = 0; x < LK_PROBE_WIDTH; ++x)
            pyr[y * LK_PROBE_WIDTH + x] = src[y * LK_PROBE_WIDTH + x];
    for (l = 1; l < LK_PROBE_LEVELS; ++l) {
        const uint8_t *p = pyr + offsets[l - 1];
        uint8_t *q = pyr + offsets[l];
        unsigned int w = widths[l], pw = widths[l - 1];
        for (y = 0; y < heights[l]; ++y)
            for (x = 0; x < w; ++x) {
                unsigned int j = 2 * y * pw + 2 * x;
                q[y * w + x] = (p[j] + p[j + 1] + p[j + pw] + p[j + pw + 1] + 2) / 4;
            }
    }
}

int lk_probe_run(void *buffer, lk_probe_clock_fn clock_fn)
{
    struct lk_probe_control *ctl = (struct lk_probe_control *)buffer;
    uint8_t *base = (uint8_t *)buffer;
    uint8_t *pa = base + LK_PROBE_PYR_PREV_OFF;
    uint8_t *pb = base + LK_PROBE_PYR_NEXT_OFF;
    uint64_t start, after_pyramid;
    unsigned int i;
    if (sizeof(*ctl) > LK_PROBE_CONTROL_BYTES || ctl->magic != LK_PROBE_MAGIC ||
        ctl->count > LK_PROBE_POINTS) return -1;
    start = clock_fn();
    make_pyramid(base + LK_PROBE_PREV_OFF, pa);
    make_pyramid(base + LK_PROBE_NEXT_OFF, pb);
    after_pyramid = clock_fn();
    ctl->valid = 0;
    ctl->total_iterations = 0;
    for (i = 0; i < ctl->count; ++i) {
        float sx = ctl->input[i].x, sy = ctl->input[i].y;
        float nx = 0.0f, ny = 0.0f;
        int ok = 1, l;
        for (l = LK_PROBE_LEVELS - 1; l >= 0; --l) {
            float scale = (float)(1u << l);
            float rx = sx / scale, ry = sy / scale;
            float patch[225], gx[225], gy[225];
            float hxx = 0.0f, hxy = 0.0f, hyy = 0.0f, det;
            const uint8_t *prev = pa + offsets[l], *next = pb + offsets[l];
            int w = (int)widths[l], h = (int)heights[l];
            int xx, yy, k = 0, it;
            if (l == LK_PROBE_LEVELS - 1) { nx = rx; ny = ry; }
            else { nx *= 2.0f; ny *= 2.0f; }
            if (rx < 0 || ry < 0 || rx >= w || ry >= h) { ok = 0; break; }
            for (yy = -7; yy <= 7; ++yy)
                for (xx = -7; xx <= 7; ++xx, ++k) {
                    float px = rx + xx, py = ry + yy;
                    patch[k] = sample(prev, w, h, px, py);
                    gx[k] = 0.5f * (sample(prev, w, h, px + 1, py) -
                                     sample(prev, w, h, px - 1, py));
                    gy[k] = 0.5f * (sample(prev, w, h, px, py + 1) -
                                     sample(prev, w, h, px, py - 1));
                    hxx += gx[k] * gx[k];
                    hxy += gx[k] * gy[k];
                    hyy += gy[k] * gy[k];
                }
            det = hxx * hyy - hxy * hxy;
            if (det < 1e-4f) { ok = 0; break; }
            for (it = 0; it < 30; ++it) {
                float bx = 0.0f, by = 0.0f, dx, dy;
                k = 0;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) { ok = 0; break; }
                for (yy = -7; yy <= 7; ++yy)
                    for (xx = -7; xx <= 7; ++xx, ++k) {
                        float residual = patch[k] - sample(next, w, h, nx + xx, ny + yy);
                        bx += gx[k] * residual;
                        by += gy[k] * residual;
                    }
                dx = (hyy * bx - hxy * by) / det;
                dy = (hxx * by - hxy * bx) / det;
                nx += dx;
                ny += dy;
                ++ctl->total_iterations;
                if (dx * dx + dy * dy < 0.0001f) break;
            }
            if (!ok) break;
        }
        ctl->mask[i] = (uint8_t)ok;
        ctl->output[i].x = nx;
        ctl->output[i].y = ny;
        ctl->valid += ok;
    }
    ctl->pyramid_ticks = after_pyramid - start;
    ctl->track_ticks = clock_fn() - after_pyramid;
    ctl->total_ticks = ctl->pyramid_ticks + ctl->track_ticks;
    ctl->status = 1;
    return 0;
}
