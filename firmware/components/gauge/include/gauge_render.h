/*
 * gauge_render.h - the gauge, drawn straight into the gfx framebuffer.
 *
 * Same separation as before: gauge_math/theme/presets hold the numbers and the
 * look, this turns them into pixels.  It redraws the whole dial each frame -
 * at 320x240 that still costs well under the SPI transfer, and it means there
 * is no cached state to get out of step with the panel.
 *
 * The dial itself is round and unchanged from the 1.28" board: a
 * GAUGE_DIAL_DIAMETER circle centred in the wider 320x240 panel, with black
 * margins either side.
 */
#pragma once

#include <stdbool.h>

#include "gauge_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Diameter of the circular dial, centred in the framebuffer. */
#define GAUGE_DIAL_DIAMETER 240

typedef struct gauge_render gauge_render_t;

gauge_render_t *gauge_render_create(const gauge_config_t *cfg);
void gauge_render_destroy(gauge_render_t *g);

/* Pushes a reading; the needle slews toward it over the next ticks. */
void gauge_render_set_value(gauge_render_t *g, float value);
/* Jumps straight there. */
void gauge_render_set_immediate(gauge_render_t *g, float value);

float gauge_render_displayed(const gauge_render_t *g);
float gauge_render_target(const gauge_render_t *g);
const gauge_config_t *gauge_render_config(const gauge_render_t *g);

/* Advance the slew by `dt` seconds and draw the dial into the framebuffer. */
void gauge_render_tick(gauge_render_t *g, float dt);
/* Draw without advancing the slew. */
void gauge_render_draw(gauge_render_t *g);

/* True while the needle is still moving. */
bool gauge_render_moving(const gauge_render_t *g);

/*
 * A small line of text under the read-out, in the theme's dim status colour -
 * used for the delivered frame rate.  NULL or "" hides it.
 */
void gauge_render_set_status(gauge_render_t *g, const char *text);

/*
 * The radii the renderer actually resolved, in pixels from the dial centre.
 * Exposed so tests and diagnostics can check the real layout instead of
 * re-deriving it and drifting out of step.
 */
typedef struct {
    int dial_radius;    /* outer edge of the bezel   */
    int r_rail;         /* outer edge of the band    */
    int r_band_in;
    int r_tick_base;    /* ticks hang inwards from here */
    int r_alarm_out;    /* warning sector            */
    int r_alarm_in;
    int tick_major_len;
    int tick_minor_len;
    int needle_len;
    int label_radius;   /* centre of the numerals    */
    int hub_radius;
} gauge_geometry_t;

void gauge_render_geometry(const gauge_render_t *g, gauge_geometry_t *out);

#ifdef __cplusplus
}
#endif
