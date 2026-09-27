/*
 * bsp.h - board support for the ESP32-2432S028R ("CYD") 2.8" display.
 *
 * Deliberately thin and dependency-free: SPI, the ILI9341/ST7789 panel, the
 * backlight, the XPT2046 touch controller, and nothing else.  There is no
 * graphics library underneath - the gfx component owns the framebuffer and
 * pushes rectangles through bsp_lcd_draw_bitmap().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The panel is 240x320 natively; the BSP brings it up in 320x240 landscape. */
#define BSP_LCD_H_RES 320
#define BSP_LCD_V_RES 240

/* Brings up SPI, the panel and the backlight.  Returns an error instead of
 * aborting so a dead panel never costs you the serial console. */
esp_err_t bsp_display_init(void);

/* NULL until bsp_display_init() succeeds. */
esp_lcd_panel_handle_t bsp_lcd_panel(void);

/* Push a rectangle of RGB565 pixels.  x1/y1 are exclusive.  Blocking. */
esp_err_t bsp_lcd_draw_bitmap(int x0, int y0, int x1, int y1, const uint16_t *pixels);

/* Fill a rectangle with one colour; uses a small internal line buffer. */
esp_err_t bsp_lcd_fill_rect(int x0, int y0, int x1, int y1, uint16_t colour);

/* 0..100 */
void bsp_backlight_set(int percent);
int  bsp_backlight_get(void);

/* Number of completed panel transfers, for diagnostics. */
uint32_t bsp_lcd_flush_count(void);

/* -------------------------------------------------------------------------- */
/* touch (XPT2046 resistive, its own SPI bus)                                 */
/* -------------------------------------------------------------------------- */

/* Brings up the touch controller's SPI bus.  Safe to call more than once. */
esp_err_t bsp_touch_init(void);

/*
 * Reads the raw 12-bit X/Y sample.  Returns false when the panel is not being
 * touched (the IRQ line is high and/or the sample is off-scale).  Raw values
 * are what the calibration in app_touch.c maps onto screen coordinates.
 */
bool bsp_touch_read_raw(int *x, int *y);

/* Last raw sample, regardless of the pressed state; for calibration output. */
void bsp_touch_last_raw(int *x, int *y, bool *pressed);

/* True while the pen is down according to the IRQ pin (active low). */
bool bsp_touch_irq_active(void);

#ifdef __cplusplus
}
#endif