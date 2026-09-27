/*
 * app_touch.h - XPT2046 touchscreen: sampling, calibration and tap events.
 *
 * The BSP reports raw 12-bit samples; this layer maps them onto screen
 * coordinates, debounces them and turns a press/release into a tap.  Taps are
 * handed to the gauge layer, which uses them to cycle instruments.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the polling task.  Returns ESP_ERR_INVALID_STATE if the BSP touch
 * controller never came up. */
esp_err_t app_touch_start(void);

/* Stops the polling task (used while a test screen owns the display). */
void app_touch_stop(void);

/* Last raw sample from the controller, for the console. */
void app_touch_last_raw(int *x, int *y, bool *pressed);

/* Last mapped screen position, for the console. */
void app_touch_last_screen(int *x, int *y, bool *pressed);

/* Number of taps seen since boot. */
unsigned app_touch_tap_count(void);

#ifdef __cplusplus
}
#endif