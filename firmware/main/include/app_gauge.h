/*
 * app_gauge.h - drives the gauge and pushes frames to the panel.
 */
#pragma once

#include <stdbool.h>

#include "gauge_presets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the gauge and starts the 20 ms driver task. */
void app_gauge_start(void);

/* Swap instrument; rebuilds the renderer. */
void app_gauge_select(const gauge_preset_t *preset);
const gauge_preset_t *app_gauge_current(void);

/* Advance to the next preset in the catalogue (used by touch taps). */
void app_gauge_next(void);

/* true  -> the built-in engine simulator drives the needle
 * false -> the needle follows app_gauge_set_value() */
void app_gauge_set_demo(bool on);
bool app_gauge_is_demo(void);

void app_gauge_set_value(float value);

/* Replay the full-scale self-test sweep. */
void app_gauge_sweep(void);

/* Delivered frame rate, smoothed, in frames per second. */
float app_gauge_fps(void);

/* Show/hide the frame-rate line under the read-out. */
void app_gauge_show_stats(bool on);
bool app_gauge_stats_shown(void);

/* Last frame's render and panel-flush times, microseconds. */
void app_gauge_timing(uint32_t *render_us, uint32_t *flush_us);

/* Stop drawing the gauge (so test screens can own the display). */
void app_gauge_stop(void);

#ifdef __cplusplus
}
#endif
