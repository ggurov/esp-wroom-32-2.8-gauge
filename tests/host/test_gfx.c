/*
 * test_gfx.c - the drawing primitives.
 *
 * The framebuffer is real; only the panel transfer is stubbed.  These tests
 * exist because a misplaced primitive shows up as a broken dial on a 1.28"
 * screen that is awkward to photograph, and is trivial to check here.
 */
#include <math.h>

#include "gfx.h"
#include "gfx_font_data.h"
#include "gfx_text.h"
#include "stub_bsp.h"
#include "test_framework.h"

#define WHITE 0xFFFF
#define BLACK 0x0000
#define RED   0xF800
#define GREEN 0x07E0
#define BLUE  0x001F

static uint16_t at(int x, int y)
{
    return gfx_framebuffer()[y * GFX_W + x];
}

/* Degrees clockwise from 3 o'clock, matching the renderer's convention. */
static void polar(int r, int deg, int *x, int *y)
{
    const double rad = deg * M_PI / 180.0;
    *x = GFX_W / 2 + (int)lround(r * cos(rad));
    *y = GFX_H / 2 + (int)lround(r * sin(rad));
}

/* Count pixels in a box that are not the background colour. */
static int ink_in(int x0, int y0, int x1, int y1, uint16_t bg)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if ((unsigned)x < GFX_W && (unsigned)y < GFX_H && at(x, y) != bg) {
                n++;
            }
        }
    }
    return n;
}

TF_TEST(gfx, clear_fills_every_pixel)
{
    gfx_clear(RED);
    for (int y = 0; y < GFX_H; y += 7) {
        for (int x = 0; x < GFX_W; x += 7) {
            TF_EQ_INT(at(x, y), RED);
        }
    }
}

TF_TEST(gfx, pixel_and_rect_clip_at_the_edges)
{
    gfx_clear(BLACK);

    gfx_pixel(-1, 0, WHITE);
    gfx_pixel(0, -1, WHITE);
    gfx_pixel(GFX_W, 0, WHITE);
    gfx_pixel(0, GFX_H, WHITE);
    gfx_pixel(0, 0, WHITE);
    TF_EQ_INT(at(0, 0), WHITE);          /* only the in-bounds one landed */

    gfx_clear(BLACK);
    gfx_fill_rect(-50, -50, 50, 50, GREEN);
    TF_EQ_INT(at(0, 0), GREEN);
    TF_EQ_INT(at(50, 50), GREEN);
    TF_EQ_INT(at(51, 51), BLACK);

    gfx_clear(BLACK);
    gfx_fill_rect(GFX_W - 10, GFX_H - 10, GFX_W + 999, GFX_H + 999, BLUE);
    TF_EQ_INT(at(GFX_W - 1, GFX_H - 1), BLUE);
    TF_EQ_INT(at(GFX_W - 11, GFX_H - 1), BLACK);
}

TF_TEST(gfx, hline_and_vline_clip)
{
    gfx_clear(BLACK);
    gfx_hline(-10, 10, 5, WHITE);
    TF_EQ_INT(at(0, 5), WHITE);
    TF_EQ_INT(at(10, 5), WHITE);
    TF_EQ_INT(at(11, 5), BLACK);

    gfx_clear(BLACK);
    gfx_vline(-10, 10, 7, WHITE);
    TF_EQ_INT(at(7, 0), WHITE);
    TF_EQ_INT(at(7, 10), WHITE);
    TF_EQ_INT(at(7, 11), BLACK);

    /* reversed arguments are normalised, not ignored */
    gfx_clear(BLACK);
    gfx_hline(10, 2, 3, WHITE);
    TF_EQ_INT(at(2, 3), WHITE);
    TF_EQ_INT(at(10, 3), WHITE);
}

TF_TEST(gfx, disc_is_round)
{
    gfx_clear(BLACK);
    gfx_disc(100, 100, 20, WHITE);

    TF_EQ_INT(at(100, 100), WHITE);      /* centre */
    TF_EQ_INT(at(100, 119), WHITE);      /* straight down, on the edge */
    TF_EQ_INT(at(119, 100), WHITE);
    TF_EQ_INT(at(100, 121), BLACK);      /* just outside */
    TF_EQ_INT(at(115, 115), BLACK);      /* corner of the box, outside the circle */
    TF_EQ_INT(at(113, 113), WHITE);      /* inside the circle (r ~= 18.4) */
}

TF_TEST(gfx, ring_leaves_the_middle_alone)
{
    gfx_clear(BLACK);
    gfx_ring(120, 120, 100, 90, WHITE);

    /* At dy = 0 the band runs [20,29] and [211,220]: the inner edge itself is
     * the boundary of the hole, so sample just outside it. */
    TF_EQ_INT(at(220, 120), WHITE);      /* r = 100, outer edge */
    TF_EQ_INT(at(211, 120), WHITE);      /* r = 91 */
    TF_EQ_INT(at(210, 120), BLACK);      /* r = 90, inside the hole */
    TF_EQ_INT(at(205, 120), BLACK);
    TF_EQ_INT(at(120, 120), BLACK);      /* centre */
    TF_EQ_INT(at(225, 120), BLACK);      /* beyond the outer edge */
}

TF_TEST(gfx, circle_outline_is_hollow)
{
    gfx_clear(BLACK);
    gfx_circle(60, 60, 40, WHITE);
    TF_EQ_INT(at(100, 60), WHITE);
    TF_EQ_INT(at(60, 60), BLACK);
    TF_EQ_INT(at(60, 100), WHITE);
}

TF_TEST(gfx, arc_band_covers_its_sweep_and_nothing_else)
{
    /*
     * A 270 degree band starting at 3 o'clock, like the gauge rail.  The band
     * is built from 2-degree quads, and a scanline filler excludes a polygon's
     * topmost row, so sample at odd angles (mid-slice) and mid-radius.
     */
    gfx_clear(BLACK);
    gfx_arc_band(GFX_W / 2, GFX_H / 2, 100, 92, 0, 270, GREEN);

    const int r = 96;                     /* midway between inner and outer */
    int x, y;

    polar(r, 89, &x, &y);
    TF_CHECK_MSG(at(x, y) == GREEN, "band missing at 89 deg (0x%04X)", at(x, y));
    polar(r, 181, &x, &y);
    TF_CHECK_MSG(at(x, y) == GREEN, "band missing at 181 deg (0x%04X)", at(x, y));
    polar(r, 269, &x, &y);
    TF_CHECK_MSG(at(x, y) == GREEN, "band missing at 269 deg (0x%04X)", at(x, y));

    /* the 90 degree gap from 270 to 360 must be bare */
    polar(r, 315, &x, &y);
    TF_CHECK_MSG(at(x, y) == BLACK, "something drawn in the gap (0x%04X)", at(x, y));

    /* inside the inner radius stays clear, outside the outer radius too */
    polar(80, 45, &x, &y);
    TF_CHECK_MSG(at(x, y) == BLACK, "band bleeds inside its inner radius");
    polar(104, 45, &x, &y);
    TF_CHECK_MSG(at(x, y) == BLACK, "band bleeds outside its outer radius");
}

TF_TEST(gfx, fill_polygon_covers_a_triangle)
{
    gfx_clear(BLACK);
    const int xs[3] = {100, 140, 100};
    const int ys[3] = {100, 100, 140};
    gfx_fill_polygon(xs, ys, 3, BLUE);

    TF_EQ_INT(at(105, 105), BLUE);       /* inside, near the right angle */
    TF_EQ_INT(at(110, 110), BLUE);
    TF_EQ_INT(at(139, 139), BLACK);      /* outside the hypotenuse */
    TF_EQ_INT(at(95, 95), BLACK);
}

TF_TEST(gfx, text_draws_ink_and_advances)
{
    gfx_clear(BLACK);
    const int w = gfx_text_width("8", &gfx_font_label);
    TF_GE(w, 4);

    /* cap-centred: the ink lands in a band of cap_height around the centre */
    const int cap = (int)gfx_font_label.cap_height;
    gfx_text_cap_centered(60, 60, "8", &gfx_font_label, WHITE);

    const int ink = ink_in(60 - w - 2, 60 - cap - 4, 60 + w + 2, 60 + cap + 4, BLACK);
    TF_CHECK_MSG(ink >= 10, "glyph barely drawn (%d px)", ink);

    /* and it must actually sit on the requested centre, not below it */
    const int above = ink_in(60 - w - 2, 60 - cap - 4, 60 + w + 2, 60 - 1, BLACK);
    const int below = ink_in(60 - w - 2, 60 + 1, 60 + w + 2, 60 + cap + 4, BLACK);
    TF_CHECK_MSG(above > 10 && below > 10,
                 "glyph not centred on the requested point (above %d, below %d)",
                 above, below);
}

TF_TEST(gfx, text_is_placed_on_the_baseline)
{
    gfx_clear(BLACK);
    /* every glyph's ink must sit above the baseline it was given */
    gfx_text(100, 100, "Hgjpq8", &gfx_font_label, WHITE);

    /* descenders (g j p q) go a little below, but the bulk is above */
    const int above = ink_in(95, 60, 200, 99, BLACK);
    TF_GE(above, 30);

    /* nothing should be drawn far below the baseline */
    const int far_below = ink_in(95, 115, 200, 150, BLACK);
    TF_EQ_INT(far_below, 0);
}

TF_TEST(gfx, text_width_grows_with_the_string)
{
    const int one = gfx_text_width("1", &gfx_font_label);
    const int four = gfx_text_width("1941", &gfx_font_label);
    const int none = gfx_text_width("", &gfx_font_label);

    TF_EQ_INT(none, 0);
    TF_GE(four, one * 3);
}

TF_TEST(gfx, flush_reports_the_rect_it_pushed)
{
    stub_bsp_reset();
    gfx_flush_rect(10, 20, 30, 40);

    int x0, y0, x1, y1;
    stub_bsp_last_rect(&x0, &y0, &x1, &y1);
    TF_EQ_INT(x0, 10);
    TF_EQ_INT(y0, 20);
    TF_EQ_INT(x1, 30);
    TF_EQ_INT(y1, 40);
    TF_EQ_INT(stub_bsp_calls(), 1);
}

TF_TEST(gfx, flush_clips_and_ignores_empty_rects)
{
    stub_bsp_reset();
    gfx_flush_rect(-100, -100, GFX_W + 100, GFX_H + 100);

    int x0, y0, x1, y1;
    stub_bsp_last_rect(&x0, &y0, &x1, &y1);
    TF_EQ_INT(x0, 0);
    TF_EQ_INT(y0, 0);
    TF_EQ_INT(x1, GFX_W);
    TF_EQ_INT(y1, GFX_H);

    stub_bsp_reset();
    gfx_flush_rect(50, 50, 50, 50);      /* zero area */
    TF_EQ_INT(stub_bsp_calls(), 0);
}

TF_TEST(gfx, blend_hits_both_ends)
{
    TF_EQ_INT(gfx_blend(RED, BLUE, 0), RED);
    TF_EQ_INT(gfx_blend(RED, BLUE, 255), BLUE);
    const uint16_t mid = gfx_blend(BLACK, WHITE, 128);
    TF_GE(mid, 0x7000);                  /* roughly half brightness */
    TF_LE(mid, 0x9000);
}
