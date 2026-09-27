/*
 * app_main.c - ESP32-2432S028R 2.8" Gauge
 *
 * The "CYD" board turned into a GReddy-flavoured automotive instrument, drawn
 * straight into an RGB565 framebuffer.  No graphics library: see README.md for
 * why LVGL was dropped.
 *
 * Boot order matters: the console comes up before the display so that a dead
 * panel, a blown SPI configuration or a bad theme can never lock you out.
 */
#include <stdio.h>

#include "app_console.h"
#include "app_gauge.h"
#include "app_tests.h"
#include "app_touch.h"
#include "bsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "gauge_presets.h"
#include "nvs_flash.h"

static const char *TAG = "app";

/*
 * White, red, green, blue - the classic panel self-test - so a colour-order or
 * inversion mistake is obvious within a second of power-up.
 */
static void panel_selftest(void)
{
    static const uint16_t flashes[] = {0xFFFF, 0xF800, 0x07E0, 0x001F};
    for (int i = 0; i < (int)(sizeof(flashes) / sizeof(flashes[0])); i++) {
        gfx_clear(flashes[i]);
        gfx_flush();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    printf("\n\n");
    ESP_LOGI(TAG, "esp-wroom-32-2.8-gauge (ESP-IDF %s, chip %s, no graphics library)",
             esp_get_idf_version(), CONFIG_IDF_TARGET);

    /* 1. Console first - the recovery path. */
    ESP_ERROR_CHECK(app_console_start());

    /* 2. Display, but never fatally. */
    err = bsp_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display init failed: %s (0x%x)", esp_err_to_name(err), err);
        ESP_LOGW(TAG, "No dial, but the console is alive.");
        ESP_LOGW(TAG, "Check pins, panel controller and clock under menuconfig -> Gauge BSP,");
        ESP_LOGW(TAG, "then reflash.");
        return;
    }

    gfx_init();

    /* 3. Prove the panel works, then put the gauge up. */
    panel_selftest();
    app_gauge_start();

    /* 4. Touch is optional: a dead controller must not cost the dial. */
    if (app_touch_start() != ESP_OK) {
        ESP_LOGW(TAG, "No touchscreen; `gauge <id>` still switches instruments.");
    }

    printf("\n");
    ESP_LOGI(TAG, "Running. `help` for commands, `gauge` to change instrument,");
    ESP_LOGI(TAG, "tap the screen to cycle gauges, `test` for bring-up screens.");
}
