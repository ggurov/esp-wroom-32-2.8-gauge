/*
 * gfx.c - framebuffer and drawing primitives.
 *
 * Everything is deliberately simple and integer-only.  No anti-aliasing: at
 * 320x240 with 3-4 px features it is not visible, and skipping it keeps the
 * code small and fast.
 */
#include "gfx.h"

#include <math.h>
#include <string.h>

#include "bsp.h"

static uint16_t s_fb[GFX_W * GFX_H] __attribute__((aligned(4)));
static uint32_t s_frames;
static uint32_t s_pixels;

void gfx_init(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

uint16_t *gfx_framebuffer(void)
{
    return s_fb;
}

uint16_t gfx_blend(uint16_t bg, uint16_t fg, uint8_t alpha)
{
    if (alpha == 0) {
        return bg;
    }
    if (alpha >= 255) {
        return fg;
    }
    const uint32_t br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    const uint32_t fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    const uint32_t r = (br * (255 - alpha) + fr * alpha) / 255;
    const uint32_t g = (bgc * (255 - alpha) + fgc * alpha) / 255;
    const uint32_t b = (bb * (255 - alpha) + fb * alpha) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* -------------------------------------------------------------------------- */

void gfx_clear(uint16_t colour)
{
    /* Two pixels per store: on a 240 MHz ESP32 the whole-screen clear is a
     * measurable slice of the frame, and this halves it. */
    uint32_t *pairs = (uint32_t *)s_fb;
    const uint32_t both = ((uint32_t)colour << 16) | colour;
    const int pairs_count = (GFX_W * GFX_H) / 2;
    for (int i = 0; i < pairs_count; i++) {
        pairs[i] = both;
    }
    if ((GFX_W * GFX_H) & 1) {
        s_fb[GFX_W * GFX_H - 1] = colour;
    }
}

void gfx_pixel(int x, int y, uint16_t colour)
{
    if ((unsigned)x < GFX_W && (unsigned)y < GFX_H) {
        s_fb[y * GFX_W + x] = colour;
    }
}

void gfx_hline(int x0, int x1, int y, uint16_t colour)
{
    if (y < 0 || y >= GFX_H) {
        return;
    }
    if (x0 > x1) {
        int t = x0; x0 = x1; x1 = t;
    }
    if (x0 < 0) x0 = 0;
    if (x1 >= GFX_W) x1 = GFX_W - 1;
    uint16_t *row = &s_fb[y * GFX_W];
    for (int x = x0; x <= x1; x++) {
        row[x] = colour;
    }
}

void gfx_vline(int y0, int y1, int x, uint16_t colour)
{
    if (x < 0 || x >= GFX_W) {
        return;
    }
    if (y0 > y1) {
        int t = y0; y0 = y1; y1 = t;
    }
    if (y0 < 0) y0 = 0;
    if (y1 >= GFX_H) y1 = GFX_H - 1;
    for (int y = y0; y <= y1; y++) {
        s_fb[y * GFX_W + x] = colour;
    }
}

void gfx_rect(int x0, int y0, int x1, int y1, uint16_t colour)
{
    gfx_hline(x0, x1, y0, colour);
    gfx_hline(x0, x1, y1, colour);
    gfx_vline(y0, y1, x0, colour);
    gfx_vline(y0, y1, x1, colour);
}

void gfx_fill_rect(int x0, int y0, int x1, int y1, uint16_t colour)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GFX_W) x1 = GFX_W - 1;
    if (y1 >= GFX_H) y1 = GFX_H - 1;

    const uint32_t both = ((uint32_t)colour << 16) | colour;
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = &s_fb[y * GFX_W];
        int x = x0;
        while (x <= x1 && (((uintptr_t)(row + x)) & 3u)) {
            row[x++] = colour;
        }
        uint32_t *pairs = (uint32_t *)(row + x);
        const int n = (x1 - x + 1) / 2;
        for (int i = 0; i < n; i++) {
            pairs[i] = both;
        }
        x += n * 2;
        while (x <= x1) {
            row[x++] = colour;
        }
    }
}

/* -------------------------------------------------------------------------- */

void gfx_line(int x0, int y0, int x1, int y1, uint16_t colour)
{
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        gfx_pixel(x0, y0, colour);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_thick_line(int x0, int y0, int x1, int y1, int width, uint16_t colour)
{
    if (width <= 1) {
        gfx_line(x0, y0, x1, y1, colour);
        return;
    }
    const int half = width / 2;
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int len = (int)sqrtf((float)(dx * dx + dy * dy));
    if (len == 0) {
        gfx_disc(x0, y0, half, colour);
        return;
    }
    /* Walk the segment stamping discs; one per half-width keeps it solid. */
    const int steps = (len / (half > 0 ? half : 1)) + 1;
    for (int i = 0; i <= steps; i++) {
        const int x = x0 + dx * i / steps;
        const int y = y0 + dy * i / steps;
        gfx_disc(x, y, half, colour);
    }
}

/* -------------------------------------------------------------------------- */

void gfx_disc(int cx, int cy, int radius, uint16_t colour)
{
    if (radius < 0) {
        return;
    }
    if (radius == 0) {
        gfx_pixel(cx, cy, colour);
        return;
    }
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        const int y = cy + dy;
        if (y < 0 || y >= GFX_H) {
            continue;
        }
        const int half = (int)sqrtf((float)(r2 - dy * dy));
        gfx_hline(cx - half, cx + half, y, colour);
    }
}

void gfx_circle(int cx, int cy, int radius, uint16_t colour)
{
    if (radius <= 0) {
        return;
    }
    int x = radius;
    int y = 0;
    int err = 1 - radius;
    while (x >= y) {
        gfx_pixel(cx + x, cy + y, colour);
        gfx_pixel(cx + y, cy + x, colour);
        gfx_pixel(cx - y, cy + x, colour);
        gfx_pixel(cx - x, cy + y, colour);
        gfx_pixel(cx - x, cy - y, colour);
        gfx_pixel(cx - y, cy - x, colour);
        gfx_pixel(cx + y, cy - x, colour);
        gfx_pixel(cx + x, cy - y, colour);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_circle_thick(int cx, int cy, int radius, int width, uint16_t colour)
{
    const int outer = radius + width / 2;
    const int inner = radius - (width - width / 2);
    for (int dy = -outer; dy <= outer; dy++) {
        const int y = cy + dy;
        if (y < 0 || y >= GFX_H) {
            continue;
        }
        const int o2 = outer * outer - dy * dy;
        if (o2 < 0) {
            continue;
        }
        const int half_out = (int)sqrtf((float)o2);
        int half_in = -1;
        if (inner > 0) {
            const int i2 = inner * inner - dy * dy;
            if (i2 >= 0) {
                half_in = (int)sqrtf((float)i2);
            }
        }
        if (half_in < 0) {
            gfx_hline(cx - half_out, cx + half_out, y, colour);
        } else {
            gfx_hline(cx - half_out, cx - half_in - 1, y, colour);
            gfx_hline(cx + half_in + 1, cx + half_out, y, colour);
        }
    }
}

void gfx_ring(int cx, int cy, int r_outer, int r_inner, uint16_t colour)
{
    if (r_inner < 0) r_inner = 0;
    for (int dy = -r_outer; dy <= r_outer; dy++) {
        const int y = cy + dy;
        if (y < 0 || y >= GFX_H) {
            continue;
        }
        const int o2 = r_outer * r_outer - dy * dy;
        if (o2 < 0) {
            continue;
        }
        const int half_out = (int)sqrtf((float)o2);
        int half_in = -1;
        if (r_inner > 0) {
            const int i2 = r_inner * r_inner - dy * dy;
            if (i2 >= 0) {
                half_in = (int)sqrtf((float)i2);
            }
        }
        if (half_in < 0) {
            gfx_hline(cx - half_out, cx + half_out, y, colour);
        } else {
            gfx_hline(cx - half_out, cx - half_in - 1, y, colour);
            gfx_hline(cx + half_in + 1, cx + half_out, y, colour);
        }
    }
}

void gfx_arc(int cx, int cy, int radius, int width, int a0, int a1, uint16_t colour)
{
    /* Walk the arc in small angular steps and stamp a disc at each one.  Step
     * size is chosen so consecutive stamps overlap for any radius. */
    int sweep = a1 - a0;
    while (sweep < 0) {
        sweep += 360;
    }
    if (sweep > 360) {
        sweep = 360;
    }

    const int steps = (int)(sweep * 4) + 1;   /* quarter-degree */
    const int half = width / 2;
    int last_x = 0, last_y = 0;
    for (int i = 0; i <= steps; i++) {
        const float deg = (float)(a0 + sweep * i / steps);
        const float rad = deg * (float)M_PI / 180.0f;
        const int x = cx + (int)lroundf(radius * cosf(rad));
        const int y = cy + (int)lroundf(radius * sinf(rad));
        if (i > 0) {
            gfx_thick_line(last_x, last_y, x, y, width, colour);
        }
        last_x = x;
        last_y = y;
    }
    (void)half;
}

/* -------------------------------------------------------------------------- */

void gfx_fill_polygon(const int *xs, const int *ys, int count, uint16_t colour)
{
    if (count < 3) {
        return;
    }

    int ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < count; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    if (ymin < 0) ymin = 0;
    if (ymax >= GFX_H) ymax = GFX_H - 1;

    int nodes[32];
    for (int y = ymin; y <= ymax; y++) {
        int n = 0;
        for (int i = 0, j = count - 1; i < count; j = i++) {
            const int yi = ys[i], yj = ys[j];
            if ((yi < y && yj >= y) || (yj < y && yi >= y)) {
                if (n < (int)(sizeof(nodes) / sizeof(nodes[0]))) {
                    nodes[n++] = xs[i] + (y - yi) * (xs[j] - xs[i]) / (yj - yi);
                }
            }
        }
        /* insertion sort - n is tiny */
        for (int a = 1; a < n; a++) {
            const int key = nodes[a];
            int b = a - 1;
            while (b >= 0 && nodes[b] > key) {
                nodes[b + 1] = nodes[b];
                b--;
            }
            nodes[b + 1] = key;
        }
        for (int a = 0; a + 1 < n; a += 2) {
            gfx_hline(nodes[a], nodes[a + 1], y, colour);
        }
    }
}

/* -------------------------------------------------------------------------- */

void gfx_arc_band(int cx, int cy, int r_outer, int r_inner, int a0, int a1,
                  uint16_t colour)
{
    if (r_inner < 0) r_inner = 0;
    if (r_outer <= r_inner) return;

    int sweep = a1 - a0;
    while (sweep < 0) {
        sweep += 360;
    }
    if (sweep == 0) {
        sweep = 360;
    }

    /* Two degrees per slice: at the radii this gauge uses that is under 4 px of
     * chord, so the band reads as a smooth arc. */
    const int slices = (sweep + 1) / 2;
    const float step = (float)sweep / (float)slices;

    for (int i = 0; i < slices; i++) {
        const float d0 = (float)a0 + step * (float)i;
        const float d1 = d0 + step;
        const float r0 = d0 * (float)M_PI / 180.0f;
        const float r1 = d1 * (float)M_PI / 180.0f;

        int px[4], py[4];
        px[0] = cx + (int)lroundf((float)r_outer * cosf(r0));
        py[0] = cy + (int)lroundf((float)r_outer * sinf(r0));
        px[1] = cx + (int)lroundf((float)r_outer * cosf(r1));
        py[1] = cy + (int)lroundf((float)r_outer * sinf(r1));
        px[2] = cx + (int)lroundf((float)r_inner * cosf(r1));
        py[2] = cy + (int)lroundf((float)r_inner * sinf(r1));
        px[3] = cx + (int)lroundf((float)r_inner * cosf(r0));
        py[3] = cy + (int)lroundf((float)r_inner * sinf(r0));
        gfx_fill_polygon(px, py, 4, colour);
    }
}

/* -------------------------------------------------------------------------- */

void gfx_flush_rect(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GFX_W) x1 = GFX_W;
    if (y1 > GFX_H) y1 = GFX_H;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    bsp_lcd_draw_bitmap(x0, y0, x1, y1, &s_fb[y0 * GFX_W + x0]);
    s_pixels += (uint32_t)(x1 - x0) * (uint32_t)(y1 - y0);
}

void gfx_flush(void)
{
    gfx_flush_rect(0, 0, GFX_W, GFX_H);
    s_frames++;
}

void gfx_get_stats(uint32_t *frames, uint32_t *pixels)
{
    if (frames) *frames = s_frames;
    if (pixels) *pixels = s_pixels;
}
