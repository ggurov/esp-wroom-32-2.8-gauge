/*
 * test_gauge_render.c - the dial itself, rendered on the host.
 *
 * gauge_render.c only touches the framebuffer, so the whole instrument can be
 * drawn and inspected on the desktop: no board, no camera, no guessing from a
 * photograph.  These check the things that are hard to see on a 1.28" round
 * panel, like whether the band is at the right radius, whether the gap is
 * where it should be, and whether the needle points where the maths says.
 */
#include <math.h>

#include "gauge_math.h"
#include "gauge_presets.h"
#include "gauge_render.h"
#include "gauge_theme.h"
#include "gfx.h"
#include "stub_bsp.h"
#include "test_framework.h"

#define CX (GFX_W / 2)
#define CY (GFX_H / 2)

static uint16_t at(int x, int y)
{
    return gfx_framebuffer()[y * GFX_W + x];
}

/* Polar helper in the renderer's convention: degrees clockwise from 3 o'clock. */
static void polar(int r, int deg, int *x, int *y)
{
    const double rad = deg * M_PI / 180.0;
    *x = CX + (int)lround(r * cos(rad));
    *y = CY + (int)lround(r * sin(rad));
}

static gauge_render_t *make_rpm(void)
{
    return gauge_render_create(gauge_preset_rpm());
}

static int count_colour(uint16_t colour, int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (at(x, y) == colour) {
                n++;
            }
        }
    }
    return n;
}

/* -------------------------------------------------------------------------- */

TF_TEST(gauge_render, create_rejects_a_bad_config)
{
    TF_CHECK(gauge_render_create(NULL) == NULL);
    gauge_config_t bad = *gauge_preset_rpm();
    bad.theme = NULL;
    TF_CHECK(gauge_render_create(&bad) == NULL);
}

TF_TEST(gauge_render, rail_radius_follows_the_theme)
{
    const gauge_theme_t *th = gauge_preset_rpm()->theme;
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);

    /* the band sits on the rail, so sample its middle from the maths */
    const int r = gauge_math_rail_radius(GAUGE_DIAL_DIAMETER, th->bezel_width, th->band_gap,
                                         th->band_width)
                  - th->band_width / 2;
    const uint16_t band = gfx_hex(th->band);

    /* 9 o'clock is inside the sweep but below the redline, so plain band */
    int x, y;
    polar(r, 180, &x, &y);
    TF_CHECK_MSG(at(x, y) == band,
                 "band missing at r=%d, 180deg (got 0x%04X, want 0x%04X)",
                 r, at(x, y), band);

    /* straight down is the 90 degree gap and must be bare face */
    polar(r, 90, &x, &y);
    TF_CHECK_MSG(at(x, y) == gfx_hex(th->face),
                 "something drawn in the gap at r=%d, 90deg (got 0x%04X)", r, at(x, y));

    gauge_render_destroy(g);
}

TF_TEST(gauge_render, redline_is_a_separate_arc_inboard_of_the_ticks)
{
    const gauge_config_t *cfg = gauge_preset_rpm();
    const gauge_theme_t *th = cfg->theme;
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);

    gauge_geometry_t geo;
    gauge_render_geometry(g, &geo);

    gauge_render_set_immediate(g, cfg->min);
    gauge_render_draw(g);

    const uint16_t alarm = gfx_hex(th->alarm);
    const uint16_t band = gfx_hex(th->band);
    const int r_alarm = (geo.r_alarm_out + geo.r_alarm_in) / 2;
    int x, y;

    /* The rail runs uninterrupted: it is still green in the redline range. */
    polar(geo.r_rail - th->band_width / 2, 0, &x, &y);
    TF_CHECK_MSG(at(x, y) == band,
                 "rail in the redline range is not green (0x%04X)", at(x, y));

    /* The warning sector is a separate arc further in. */
    polar(r_alarm, 0, &x, &y);
    TF_CHECK_MSG(at(x, y) == alarm,
                 "no warning arc at r=%d, 0 deg (0x%04X, want 0x%04X)",
                 r_alarm, at(x, y), alarm);

    /* Outside the redline range the same radius is bare face. */
    polar(r_alarm, 181, &x, &y);
    TF_CHECK_MSG(at(x, y) != alarm,
                 "warning arc drawn at 181 deg, which is below the redline");

    /* And the ticks are never recoloured: a major tick past the redline start
     * is still green. */
    polar(geo.r_tick_base - 2, 0, &x, &y);
    TF_CHECK_MSG(at(x, y) != alarm,
                 "a major tick was painted in the alarm colour; the warning "
                 "sector must not cover the ticks");

    gauge_render_destroy(g);
}

TF_TEST(gauge_render, hub_covers_the_centre)
{
    const gauge_theme_t *th = gauge_preset_rpm()->theme;
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);

    gauge_render_set_immediate(g, 4000);
    gauge_render_draw(g);

    TF_EQ_INT(at(CX, CY), gfx_hex(th->needle_hub));
    TF_EQ_INT(at(CX + 2, CY), gfx_hex(th->needle_hub));

    gauge_render_destroy(g);
}

TF_TEST(gauge_render, needle_points_where_the_maths_says)
{
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);

    const uint16_t needle = gfx_hex(gauge_preset_rpm()->theme->needle);

    /* 4000 of 0..8000 is exactly half the sweep, i.e. straight up */
    gauge_render_set_immediate(g, 4000.0f);
    gauge_render_draw(g);
    TF_CHECK_MSG(at(CX, CY - 60) == needle,
                 "needle not straight up at 4000 rpm (got 0x%04X)", at(CX, CY - 60));
    TF_CHECK_MSG(at(CX, CY + 60) != needle, "needle also drawn pointing down");

    /* minimum points down-left, maximum down-right */
    gauge_render_set_immediate(g, 0.0f);
    gauge_render_draw(g);
    TF_CHECK_MSG(at(CX - 42, CY + 42) == needle, "needle not at 7:30 for 0 rpm");

    gauge_render_set_immediate(g, 8000.0f);
    gauge_render_draw(g);
    TF_CHECK_MSG(at(CX + 42, CY + 42) == needle, "needle not at 4:30 for 8000 rpm");

    gauge_render_destroy(g);
}

TF_TEST(gauge_render, needle_never_leaves_the_dial)
{
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);
    const uint16_t needle = gfx_hex(gauge_preset_rpm()->theme->needle);

    /* sweep slowly and make sure the blade tip tracks a constant radius */
    for (float v = 0.0f; v <= 8000.0f; v += 250.0f) {
        gauge_render_set_immediate(g, v);
        gauge_render_draw(g);

        /* count needle pixels and make sure they are all inside the rail */
        int found = 0;
        for (int y = 0; y < GFX_H; y += 2) {
            for (int x = 0; x < GFX_W; x += 2) {
                if (at(x, y) != needle) {
                    continue;
                }
                found++;
                const int dx = x - CX, dy = y - CY;
                TF_CHECK_MSG(dx * dx + dy * dy <= 100 * 100,
                             "needle pixel at (%d,%d) is outside the dial for %.0f rpm",
                             x, y, (double)v);
            }
        }
        TF_CHECK_MSG(found > 20, "needle almost invisible at %.0f rpm (%d px)",
                     (double)v, found);
    }

    gauge_render_destroy(g);
}

TF_TEST(gauge_render, read_out_is_drawn_in_the_value_colour)
{
    const gauge_theme_t *th = gauge_preset_rpm()->theme;
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);

    gauge_render_set_immediate(g, 4250.0f);
    gauge_render_draw(g);

    /* the value sits between the hub and the numerals, below centre */
    const int ink = count_colour(gfx_hex(th->value), CX - 70, CY + 10, CX + 70, CY + 70);
    TF_CHECK_MSG(ink > 40, "read-out barely drawn (%d px in the value colour)", ink);

    gauge_render_destroy(g);
}

TF_TEST(gauge_render, a_unit_line_is_optional)
{
    /* the rpm preset no longer ships one; make sure nothing is drawn high in
     * the lower wedge where it used to be */
    const gauge_theme_t *th = gauge_preset_rpm()->theme;
    TF_CHECK(gauge_preset_rpm()->unit == NULL);

    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);
    gauge_render_set_immediate(g, 4250.0f);
    gauge_render_draw(g);
    const int ink = count_colour(gfx_hex(th->unit), CX - 70, CY + 60, CX + 70, CY + 90);
    TF_CHECK_MSG(ink < 5, "unit colour still drawn at the bottom (%d px)", ink);
    gauge_render_destroy(g);
}

/* -------------------------------------------------------------------------- */

TF_TEST(gauge_render, needle_slews_instead_of_jumping)
{
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);
    const gauge_config_t *cfg = gauge_render_config(g);

    gauge_render_set_immediate(g, cfg->min);
    gauge_render_set_value(g, cfg->max);

    TF_CHECK(gauge_render_moving(g));
    TF_NEAR(gauge_render_displayed(g), cfg->min, 1e-3);

    gauge_render_tick(g, 0.016f);
    const float after_one = gauge_render_displayed(g);
    TF_GE(after_one, cfg->min);
    TF_LE(after_one, cfg->max * 0.2f);

    /* it must settle, and stop reporting movement */
    for (int i = 0; i < 400; i++) {
        gauge_render_tick(g, 0.016f);
    }
    TF_NEAR(gauge_render_displayed(g), cfg->max, 1.0);
    TF_CHECK(!gauge_render_moving(g));

    gauge_render_destroy(g);
}

TF_TEST(gauge_render, values_are_clamped)
{
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);
    const gauge_config_t *cfg = gauge_render_config(g);

    gauge_render_set_value(g, -9999.0f);
    TF_NEAR(gauge_render_target(g), cfg->min, 1e-3);

    gauge_render_set_value(g, 99999.0f);
    TF_NEAR(gauge_render_target(g), cfg->max, 1e-3);

    gauge_render_destroy(g);
}

/* -------------------------------------------------------------------------- */

TF_TEST(gauge_render, every_preset_draws_without_leaving_the_frame)
{
    for (const gauge_preset_t *p = gauge_presets_all(); p->id; p++) {
        gfx_clear(gfx_hex(p->cfg->theme->face));
        gauge_render_t *g = gauge_render_create(p->cfg);
        TF_CHECK_MSG(g != NULL, "%s: create failed", p->id);
        if (!g) {
            continue;
        }

        /* both ends of the range, since the needle sweep is the risky part */
        gauge_render_set_immediate(g, p->cfg->min);
        gauge_render_draw(g);
        gauge_render_set_immediate(g, p->cfg->max);
        gauge_render_draw(g);

        /* the corners of the square must stay background: the dial is round */
        const uint16_t face = gfx_hex(p->cfg->theme->face);
        TF_CHECK_MSG(at(2, 2) == face, "%s: ink in the top-left corner (0x%04X)",
                     p->id, at(2, 2));
        TF_CHECK_MSG(at(GFX_W - 3, GFX_H - 3) == face,
                     "%s: ink in the bottom-right corner (0x%04X)", p->id, at(GFX_W - 3, GFX_H - 3));

        gauge_render_destroy(g);
    }
}

TF_TEST(gauge_render, dial_is_centred_on_the_rectangle)
{
    const gauge_theme_t *th = gauge_preset_rpm()->theme;
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);

    gauge_render_set_immediate(g, 4000.0f);
    gauge_render_draw(g);

    /* the dial is a 240 px circle centred in the 320x240 panel: the bezel is
     * at the top of the circle, and the side margins stay face-coloured */
    TF_CHECK_MSG(at(CX, CY - (GAUGE_DIAL_DIAMETER / 2 - 1)) == gfx_hex(th->bezel),
                 "bezel missing at the top of the dial");
    TF_CHECK_MSG(at(2, CY) == gfx_hex(th->face), "ink in the left margin");
    TF_CHECK_MSG(at(GFX_W - 3, CY) == gfx_hex(th->face), "ink in the right margin");

    gauge_render_destroy(g);
}

TF_TEST(gauge_render, geometry_is_layered_without_overlaps)
{
    gauge_render_t *g = make_rpm();
    TF_REQUIRE(g != NULL);

    gauge_geometry_t geo;
    gauge_render_geometry(g, &geo);

    /* outside to inside: bezel, rail, ticks, warning sector, numerals, hub */
    TF_GE(geo.dial_radius, geo.r_rail);
    TF_GE(geo.r_rail, geo.r_band_in);
    TF_CHECK_MSG(geo.r_tick_base <= geo.r_band_in,
                 "ticks (%d) start outside the rail's inner edge (%d)",
                 geo.r_tick_base, geo.r_band_in);

    const int tick_inner = geo.r_tick_base - geo.tick_major_len;
    TF_CHECK_MSG(geo.r_alarm_out < tick_inner,
                 "warning sector (%d) overlaps the ticks (inner %d)",
                 geo.r_alarm_out, tick_inner);
    TF_GE(geo.r_alarm_out, geo.r_alarm_in);

    TF_CHECK_MSG(geo.label_radius + 8 <= geo.r_alarm_in,
                 "numerals (%d) overlap the warning sector (%d)",
                 geo.label_radius, geo.r_alarm_in);
    TF_CHECK_MSG(geo.label_radius - 8 > geo.hub_radius,
                 "numerals collide with the hub");
    TF_CHECK_MSG(geo.needle_len <= geo.r_alarm_in,
                 "needle (%d) reaches into the warning sector", geo.needle_len);
    TF_GE(geo.needle_len, geo.label_radius);

    gauge_render_destroy(g);
}

TF_TEST(gauge_render, destroy_handles_null)
{
    gauge_render_destroy(NULL);   /* must not crash */
    TF_CHECK(gauge_render_displayed(NULL) == 0.0f);
    TF_CHECK(gauge_render_target(NULL) == 0.0f);
    TF_CHECK(gauge_render_config(NULL) == NULL);
    TF_CHECK(!gauge_render_moving(NULL));
}
