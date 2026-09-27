/*
 * app_gauge.c - drives the gauge and pushes frames to the panel.
 *
 * A 20 ms task slews the needle and redraws the dial; a full 240x240 RGB565
 * frame is 115 KB, which is about 23 ms of SPI at 40 MHz, so the frame rate
 * settles around 25-30 fps.  The render itself is a couple of milliseconds.
 *
 * Real data will arrive through app_gauge_set_value(); today the source is a
 * small engine simulator with a self-test sweep on boot.
 */
#include "app_gauge.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gauge_render.h"
#include "gfx.h"

static const char *TAG = "gauge";

#define MIN_FRAME_MS 2   /* shortest frame period the loop will run at */

/* Self-test sweep timings (ms from start). */
#define SELFTEST_TOP_MS 1150
#define SELFTEST_END_MS 1750

/* Repeating driving script, as fractions of the gauge's range so the same
 * script looks sensible on a tach, a boost gauge or a temperature gauge. */
typedef struct {
    uint16_t ms;
    float    frac;
} demo_step_t;

static const demo_step_t k_script[] = {
    { 1400, 0.11f }, {  600, 0.40f }, {  900, 0.375f }, {  500, 0.22f },
    {  700, 0.52f }, { 1000, 0.65f }, {  600, 0.575f }, {  900, 0.82f },
    {  700, 1.00f }, {  500, 0.65f }, {  800, 0.475f }, {  600, 0.30f },
    { 1200, 0.105f }, { 1000, 0.70f }, {  700, 0.375f }, { 1500, 0.115f },
};
#define SCRIPT_LEN (sizeof(k_script) / sizeof(k_script[0]))

static gauge_render_t       *s_gauge;
static const gauge_preset_t *s_preset;
static TaskHandle_t          s_task;
static bool                  s_running;      /* task alive */
static bool                  s_demo = true;
static bool                  s_visible = true;
/* true while the task is NOT touching the framebuffer/SPI, so a test screen
 * can take over without racing the panel transfer */
static volatile bool         s_idle = true;

static uint32_t s_elapsed_ms;
static size_t   s_step;
static uint32_t s_step_ms;
static float    s_jitter;
static bool     s_selftest;
static float    s_manual;

/* delivered frame rate, smoothed */
static int64_t  s_last_frame_us;
static float    s_fps;
static bool     s_show_stats = true;
static volatile uint32_t s_render_us;
static volatile uint32_t s_flush_us;

/* -------------------------------------------------------------------------- */

static void restart_selftest(void)
{
    s_elapsed_ms = 0;
    s_selftest = true;
    s_step = 0;
    s_step_ms = 0;
}

static void gauge_task(void *arg)
{
    (void)arg;
    int64_t last_us = esp_timer_get_time();

    for (;;) {
        /*
         * Yield briefly rather than sleeping a fixed frame time.  The frame
         * period is whatever the render plus the SPI transfer costs, so the
         * panel sets the frame rate and this delay only has to be short enough
         * not to be the bottleneck.  Sleeping a fixed 20 ms here cost about
         * half the available frame budget.
         */
        vTaskDelay(pdMS_TO_TICKS(MIN_FRAME_MS));

        const int64_t now = esp_timer_get_time();
        const float dt = (float)(now - last_us) / 1000000.0f;
        if (dt < (float)MIN_FRAME_MS / 1000.0f) {
            continue;
        }
        last_us = now;
        const uint32_t dt_ms = (uint32_t)(dt * 1000.0f);

        if (!s_gauge || !s_visible) {
            s_idle = true;
            continue;
        }

        s_idle = false;
        const gauge_config_t *cfg = gauge_render_config(s_gauge);

        if (!s_demo) {
            gauge_render_set_value(s_gauge, s_manual);
        } else if (s_selftest) {
            s_elapsed_ms += dt_ms;
            if (s_elapsed_ms < 500) {
                gauge_render_set_value(s_gauge, cfg->min);
            } else if (s_elapsed_ms < SELFTEST_TOP_MS) {
                gauge_render_set_value(s_gauge, cfg->max);
            } else if (s_elapsed_ms < SELFTEST_END_MS) {
                gauge_render_set_value(s_gauge, cfg->min);
            } else {
                s_selftest = false;
                s_step = 0;
                s_step_ms = 0;
            }
        } else {
            s_step_ms += dt_ms;
            if (s_step_ms >= k_script[s_step].ms) {
                s_step_ms = 0;
                s_step = (s_step + 1) % SCRIPT_LEN;
            }
            const float range = cfg->max - cfg->min;
            float value = cfg->min + k_script[s_step].frac * range;

            /* a little smoothed noise so the needle never looks generated */
            const float r = ((float)(rand() % 2001) / 1000.0f) - 1.0f;
            s_jitter += (r - s_jitter) * 0.25f;
            value += s_jitter * 0.012f * range;

            gauge_render_set_value(s_gauge, value);
        }

        /* split the frame time so the console can show where it goes:
         * render (CPU, into the framebuffer) vs flush (SPI to the panel) */
        const int64_t render_start = esp_timer_get_time();
        gauge_render_tick(s_gauge, dt);
        const int64_t flush_start = esp_timer_get_time();
        gfx_flush();
        const int64_t flush_end = esp_timer_get_time();
        s_render_us = (uint32_t)(flush_start - render_start);
        s_flush_us = (uint32_t)(flush_end - flush_start);

        /*
         * Frame rate is measured from the interval between completed frames,
         * so it includes the render and the SPI transfer - it is what the
         * panel actually gets, not what the renderer alone could do.
         */
        const int64_t done = esp_timer_get_time();
        if (s_last_frame_us) {
            const int64_t delta = done - s_last_frame_us;
            if (delta > 0) {
                const float instant = 1000000.0f / (float)delta;
                s_fps = (s_fps <= 0.0f) ? instant : (s_fps + (instant - s_fps) * 0.15f);
            }
        }
        s_last_frame_us = done;

        if (s_show_stats) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%.1f fps", (double)s_fps);
            gauge_render_set_status(s_gauge, buf);
        }

        s_idle = true;
    }
}

static void ensure_task(void)
{
    if (!s_task) {
        xTaskCreatePinnedToCore(gauge_task, "gauge", 4096, NULL, 5, &s_task, 1);
    }
}

/* -------------------------------------------------------------------------- */

void app_gauge_start(void)
{
    if (!s_preset) {
        s_preset = gauge_preset_find("rpm");
    }
    if (!s_gauge) {
        s_gauge = gauge_render_create(s_preset->cfg);
        if (!s_gauge) {
            ESP_LOGE(TAG, "gauge_render_create failed for '%s'", s_preset->id);
            return;
        }
        ESP_LOGI(TAG, "gauge: %s", s_preset->name);
    }
    s_visible = true;
    s_running = true;
    restart_selftest();
    ensure_task();
}

void app_gauge_stop(void)
{
    s_visible = false;
    /* Wait for the frame in flight to finish: the test screens drive the same
     * SPI panel, and two tasks inside esp_lcd_panel_draw_bitmap at once is a
     * reset waiting to happen. */
    for (int i = 0; i < 50 && !s_idle; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void app_gauge_select(const gauge_preset_t *preset)
{
    if (!preset || !preset->cfg) {
        return;
    }
    s_preset = preset;
    if (s_gauge) {
        gauge_render_destroy(s_gauge);
        s_gauge = NULL;
    }
    s_gauge = gauge_render_create(preset->cfg);
    if (!s_gauge) {
        ESP_LOGE(TAG, "gauge_render_create failed for '%s'", preset->id);
        return;
    }
    s_visible = true;
    ESP_LOGI(TAG, "gauge: %s", preset->name);
    restart_selftest();
    ensure_task();
}

const gauge_preset_t *app_gauge_current(void)
{
    return s_preset;
}

void app_gauge_next(void)
{
    const int count = gauge_presets_count();
    if (count < 1) {
        return;
    }
    int index = 0;
    for (int i = 0; i < count; i++) {
        if (gauge_preset_at(i) == s_preset) {
            index = i;
            break;
        }
    }
    app_gauge_select(gauge_preset_at((index + 1) % count));
}

void app_gauge_set_demo(bool on)
{
    s_demo = on;
    if (on) {
        restart_selftest();
    }
}

bool app_gauge_is_demo(void)
{
    return s_demo;
}

void app_gauge_set_value(float value)
{
    s_manual = value;
    s_demo = false;
}

void app_gauge_sweep(void)
{
    s_demo = true;
    restart_selftest();
}

float app_gauge_fps(void)
{
    return s_fps;
}

void app_gauge_show_stats(bool on)
{
    s_show_stats = on;
    if (!on && s_gauge) {
        gauge_render_set_status(s_gauge, NULL);
    }
}

bool app_gauge_stats_shown(void)
{
    return s_show_stats;
}

void app_gauge_timing(uint32_t *render_us, uint32_t *flush_us)
{
    if (render_us) *render_us = s_render_us;
    if (flush_us) *flush_us = s_flush_us;
}
