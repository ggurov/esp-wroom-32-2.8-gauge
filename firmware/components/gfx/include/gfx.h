/*
 * gfx.h - a 320x240 RGB565 framebuffer and just enough primitives to draw an
 * instrument.
 *
 * No graphics library: LVGL cost 869 KB of firmware on the round board and did
 * not render that panel any better than direct drawing does.  Everything here
 * writes into one buffer which is pushed to the panel when a frame is finished.
 *
 * Coordinates are 0..319 across, 0..239 down, with y increasing downwards.
 * All primitives clip.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_W 320
#define GFX_H 240

/* Pack 8-bit components into the panel's RGB565. */
static inline uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* 0xRRGGBB, the same form the themes and the host preview use. */
static inline uint16_t gfx_hex(uint32_t rgb)
{
    return gfx_rgb((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}

/* Blends `fg` over `bg`; 0 = bg, 255 = fg.  Used for the glow and dim text. */
uint16_t gfx_blend(uint16_t bg, uint16_t fg, uint8_t alpha);

/*
 * Cosine/sine of an integer angle in degrees, gfx convention (0 = 3 o'clock,
 * angles increase clockwise).  Backed by a 360-entry table: the renderers call
 * these hundreds of times per frame and libm's sinf/cosf are soft-float on the
 * ESP32.
 */
float gfx_cos_deg(int deg);
float gfx_sin_deg(int deg);

void gfx_init(void);
uint16_t *gfx_framebuffer(void);

void gfx_clear(uint16_t colour);
void gfx_pixel(int x, int y, uint16_t colour);
void gfx_hline(int x0, int x1, int y, uint16_t colour);
void gfx_vline(int y0, int y1, int x, uint16_t colour);
void gfx_rect(int x0, int y0, int x1, int y1, uint16_t colour);
void gfx_fill_rect(int x0, int y0, int x1, int y1, uint16_t colour);

void gfx_line(int x0, int y0, int x1, int y1, uint16_t colour);
/* A line with round-ish ends, `width` pixels across. */
void gfx_thick_line(int x0, int y0, int x1, int y1, int width, uint16_t colour);

void gfx_circle(int cx, int cy, int radius, uint16_t colour);
void gfx_circle_thick(int cx, int cy, int radius, int width, uint16_t colour);
void gfx_disc(int cx, int cy, int radius, uint16_t colour);
void gfx_ring(int cx, int cy, int r_outer, int r_inner, uint16_t colour);

/*
 * A thick arc.  Angles are degrees, 0 = 3 o'clock, increasing clockwise, which
 * is the same convention the gauge maths and LVGL use.  `a0 > a1` wraps.
 */
void gfx_arc(int cx, int cy, int radius, int width, int a0, int a1, uint16_t colour);

/* Filled convex or concave polygon, scanline filled. */
void gfx_fill_polygon(const int *xs, const int *ys, int count, uint16_t colour);

/*
 * A filled annulus sector - the gauge's glowing band.  Much cheaper than
 * stamping a thick arc, and it gives square ends, which is what an instrument
 * band looks like.
 */
void gfx_arc_band(int cx, int cy, int r_outer, int r_inner, int a0, int a1,
                  uint16_t colour);

/* Blit the whole framebuffer, or just a rectangle of it, to the panel. */
void gfx_flush(void);
void gfx_flush_rect(int x0, int y0, int x1, int y1);

/* Counters for the console. */
void gfx_get_stats(uint32_t *frames, uint32_t *pixels);

#ifdef __cplusplus
}
#endif
