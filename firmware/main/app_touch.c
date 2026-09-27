/*
 * app_touch.c - touch sampling and tap detection.
 *
 * The CYD's XPT2046 is a resistive panel: raw 12-bit values need a linear map
 * onto the 320x240 screen, and the axes are transposed relative to the display
 * in landscape.  The defaults below match a stock board; `touch watch` on the
 * console prints raw samples if a particular unit needs different numbers.
 */
#include "app_touch.h"

#include <stdio.h>

#include "app_gauge.h"
#include "bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

#define POLL_MS        20     /* sampling period                        */
#define DEBOUNCE       2      /* consecutive samples to accept a state  */
#define TAP_MAX_MS     900    /* longer than this is a press, not a tap */
#define TAP_SLOP       28     /* px of movement still counted as a tap  */

typedef struct {
    int  x_min, x_max;        /* raw range across the screen            */
    int  y_min, y_max;        /* raw range down the screen              */
    bool swap;                /* the touch axes are transposed vs. LCD  */
    bool invert_x;
    bool invert_y;
} touch_cal_t;

/* Stock ESP32-2432S028R: X and Y are swapped relative to the landscape LCD,
 * and the Y axis runs the other way.  Values are deliberately wide so a tap
 * lands on the right half of the screen even on an uncalibrated unit. */
static touch_cal_t s_cal = {
    .x_min = 250, .x_max = 3800,
    .y_min = 250, .y_max = 3800,
    .swap = true,
    .invert_x = false,
    .invert_y = true,
};

static TaskHandle_t s_task;

static volatile int  s_raw_x, s_raw_y;
static volatile bool s_raw_pressed;
static volatile int  s_scr_x, s_scr_y;
static volatile bool s_scr_pressed;
static volatile unsigned s_taps;

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int scale(int v, int lo, int hi, int size)
{
    if (hi <= lo) {
        return 0;
    }
    return clamp_int((v - lo) * size / (hi - lo), 0, size - 1);
}

static void map_raw(int raw_x, int raw_y, int *x, int *y)
{
    int a, b;
    if (s_cal.swap) {
        a = scale(raw_y, s_cal.y_min, s_cal.y_max, BSP_LCD_H_RES);
        b = scale(raw_x, s_cal.x_min, s_cal.x_max, BSP_LCD_V_RES);
    } else {
        a = scale(raw_x, s_cal.x_min, s_cal.x_max, BSP_LCD_H_RES);
        b = scale(raw_y, s_cal.y_min, s_cal.y_max, BSP_LCD_V_RES);
    }
    *x = s_cal.invert_x ? (BSP_LCD_H_RES - 1 - a) : a;
    *y = s_cal.invert_y ? (BSP_LCD_V_RES - 1 - b) : b;
}

static void touch_task(void *arg)
{
    (void)arg;

    int stable = 0;
    bool last_state = false;
    bool pressed = false;
    int down_x = 0, down_y = 0;
    TickType_t down_at = 0;

    for (;;) {
        int rx = 0, ry = 0;
        const bool now = bsp_touch_read_raw(&rx, &ry);

        s_raw_x = rx;
        s_raw_y = ry;
        s_raw_pressed = now;

        if (now == last_state) {
            if (stable < DEBOUNCE) {
                stable++;
            }
        } else {
            stable = 0;
            last_state = now;
        }

        if (stable >= DEBOUNCE) {
            if (now && !pressed) {
                pressed = true;
                map_raw(rx, ry, &down_x, &down_y);
                down_at = xTaskGetTickCount();
            } else if (!now && pressed) {
                pressed = false;
                int x, y;
                map_raw(rx, ry, &x, &y);
                const int dx = x - down_x, dy = y - down_y;
                const uint32_t held_ms =
                    (uint32_t)((xTaskGetTickCount() - down_at) * portTICK_PERIOD_MS);
                if (held_ms <= TAP_MAX_MS && dx * dx + dy * dy <= TAP_SLOP * TAP_SLOP) {
                    s_taps++;
                    ESP_LOGI(TAG, "tap %u at %d,%d (held %u ms)",
                             s_taps, x, y, (unsigned)held_ms);
                    app_gauge_next();
                }
            }
        }

        if (stable >= DEBOUNCE && now) {
            int mx, my;
            map_raw(rx, ry, &mx, &my);
            s_scr_x = mx;
            s_scr_y = my;
            s_scr_pressed = true;
        } else if (!now) {
            s_scr_pressed = false;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t app_touch_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    esp_err_t err = bsp_touch_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch controller not available: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreatePinnedToCore(touch_task, "touch", 3072, NULL, 4, &s_task, 0) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "touch sampling at %d Hz, tap = next gauge", 1000 / POLL_MS);
    return ESP_OK;
}

void app_touch_stop(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
}

void app_touch_last_raw(int *x, int *y, bool *pressed)
{
    if (x) *x = s_raw_x;
    if (y) *y = s_raw_y;
    if (pressed) *pressed = s_raw_pressed;
}

void app_touch_last_screen(int *x, int *y, bool *pressed)
{
    if (x) *x = s_scr_x;
    if (y) *y = s_scr_y;
    if (pressed) *pressed = s_scr_pressed;
}

unsigned app_touch_tap_count(void)
{
    return s_taps;
}