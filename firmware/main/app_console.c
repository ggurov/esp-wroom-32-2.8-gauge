/*
 * app_console.c - interactive console on the CH340 UART (UART0, 115200).
 *
 * This is the board's lifeline: the REPL comes up before the display, so a
 * dead panel can never lock you out.  The CYD auto-downloads over the CH340's
 * DTR/RTS lines, so reflashing normally needs no console command at all - but
 * `bootloader` stays as a recovery path.
 */
#include "app_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_gauge.h"
#include "app_tests.h"
#include "app_touch.h"
#include "bsp.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gauge_presets.h"
#include "gfx.h"
#include "soc/rtc_cntl_reg.h"

static const char *TAG = "console";

/* -------------------------------------------------------------------------- */

void app_reboot_to_bootloader(void)
{
    printf("\nRebooting into ROM download mode...\n");
    printf("The board will sit in the bootloader until you flash it.\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));

#if defined(RTC_CNTL_FORCE_DOWNLOAD_BOOT) && defined(RTC_CNTL_OPTION1_REG)
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
#else
    printf("This target has no force-download-boot bit; restarting normally.\n");
    esp_restart();
#endif
}

static int cmd_bootloader(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_reboot_to_bootloader();
    return 0;   /* not reached */
}

static int cmd_reset(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Restarting...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
    return 0;
}

static int cmd_test(int argc, char **argv)
{
    if (argc < 2) {
        printf("Test screens: fill, bars, grid, circle, quad\n");
        printf("`next` cycles; a bare `test` redraws the current one.\n");
        return 0;
    }
    app_show_test(argv[1]);
    return 0;
}

static int cmd_next(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_next_test();
    return 0;
}

static int cmd_backlight(int argc, char **argv)
{
    if (argc < 2) {
        printf("Backlight %d%%\n", bsp_backlight_get());
        return 0;
    }
    bsp_backlight_set(atoi(argv[1]));
    printf("Backlight -> %d%%\n", bsp_backlight_get());
    return 0;
}

static int cmd_flush(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t frames = 0, pixels = 0;
    gfx_get_stats(&frames, &pixels);
    printf("full frames pushed : %u\n", (unsigned)frames);
    printf("panel transfers    : %u\n", (unsigned)bsp_lcd_flush_count());
    printf("pixels pushed      : %u\n", (unsigned)pixels);
    return 0;
}

static int cmd_gauge(int argc, char **argv)
{
    if (argc < 2) {
        const gauge_preset_t *cur = app_gauge_current();
        printf("Available gauges:\n");
        for (const gauge_preset_t *p = gauge_presets_all(); p->id; p++) {
            printf("  %-6s %s%s\n", p->id, p->name,
                   (cur && strcmp(cur->id, p->id) == 0) ? "   <- active" : "");
        }
        printf("Usage: gauge <id>\n");
        return 0;
    }
    const gauge_preset_t *p = gauge_preset_find(argv[1]);
    if (!p) {
        printf("Unknown gauge '%s'. Run `gauge` for the list.\n", argv[1]);
        return 1;
    }
    app_gauge_select(p);
    printf("Gauge -> %s\n", p->name);
    return 0;
}

static int cmd_demo(int argc, char **argv)
{
    if (argc < 2) {
        printf("Simulator is %s\n", app_gauge_is_demo() ? "ON" : "OFF");
        return 0;
    }
    if (strcmp(argv[1], "on") == 0) {
        app_gauge_set_demo(true);
        printf("Simulator on\n");
    } else if (strcmp(argv[1], "off") == 0) {
        app_gauge_set_demo(false);
        printf("Simulator off - use `value <n>`\n");
    } else if (strcmp(argv[1], "sweep") == 0) {
        app_gauge_sweep();
        printf("Self-test sweep\n");
    } else {
        printf("Usage: demo [on|off|sweep]\n");
        return 1;
    }
    return 0;
}

static int cmd_value(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: value <number>\n");
        return 1;
    }
    const float v = strtof(argv[1], NULL);
    app_gauge_set_value(v);
    printf("Value -> %.2f (simulator off)\n", (double)v);
    return 0;
}

static int cmd_fps(int argc, char **argv)
{
    if (argc >= 2) {
        if (strcmp(argv[1], "on") == 0) {
            app_gauge_show_stats(true);
        } else if (strcmp(argv[1], "off") == 0) {
            app_gauge_show_stats(false);
        } else {
            printf("usage: fps [on|off]\n");
            return 1;
        }
    }
    printf("delivered frame rate: %.1f fps  (readout %s)\n",
           (double)app_gauge_fps(),
           app_gauge_stats_shown() ? "shown" : "hidden");
    uint32_t render_us = 0, flush_us = 0;
    app_gauge_timing(&render_us, &flush_us);
    printf("last frame: %.1f ms render + %.1f ms panel flush\n",
           (double)render_us / 1000.0, (double)flush_us / 1000.0);
    return 0;
}

static int cmd_touch(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "watch") == 0) {
        const int seconds = (argc >= 3) ? atoi(argv[2]) : 10;
        printf("Watching touch for up to %d s - tap or hold the panel.\n", seconds);
        printf("    raw x    raw y   ->  screen x  screen y  state\n");
        for (int i = 0; i < seconds * 10; i++) {
            int rx, ry, sx, sy;
            bool pressed;
            app_touch_last_raw(&rx, &ry, &pressed);
            app_touch_last_screen(&sx, &sy, &pressed);
            printf("  %6d   %6d   ->   %6d    %6d   %s\n",
                   rx, ry, sx, sy, pressed ? "down" : "up");
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        printf("done (%u taps so far)\n", app_touch_tap_count());
        return 0;
    }

    int rx, ry, sx, sy;
    bool pressed;
    app_touch_last_raw(&rx, &ry, &pressed);
    app_touch_last_screen(&sx, &sy, &pressed);
    printf("raw %d,%d -> screen %d,%d (%s), %u taps\n",
           rx, ry, sx, sy, pressed ? "down" : "up", app_touch_tap_count());
    printf("`touch watch [s]` prints live samples for calibration.\n");
    return 0;
}

static int cmd_free(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Internal heap : %u B free / %u B total (min ever %u B)\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    printf("Largest block : %u B\n",
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    printf("Framebuffer   : %u B static\n", (unsigned)(GFX_W * GFX_H * 2));
    return 0;
}

static int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("esp-wroom-32-2.8-gauge  |  ESP-IDF %s  |  no graphics library\n",
           esp_get_idf_version());
    printf("Target: %s   Cores: %d   CPU: %d MHz (configured)\n",
           CONFIG_IDF_TARGET, portNUM_PROCESSORS, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#if CONFIG_BSP_PANEL_ILI9341
    const char *panel = "ILI9341";
#else
    const char *panel = "ST7789V";
#endif
    printf("Panel: %dx%d %s, %d MHz SPI, landscape\n",
           BSP_LCD_H_RES, BSP_LCD_V_RES, panel, CONFIG_BSP_LCD_SPI_CLK_MHZ);
    return 0;
}

/* -------------------------------------------------------------------------- */

esp_err_t app_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "gauge>";
    repl_cfg.max_cmdline_length = 128;
    repl_cfg.task_stack_size = 4096;

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        return err;
    }

    static const esp_console_cmd_t cmds[] = {
        { .command = "bootloader", .help = "Reboot into ROM download mode (for idf.py flash)",
          .func = &cmd_bootloader },
        { .command = "reset", .help = "Restart the application", .func = &cmd_reset },
        { .command = "test", .help = "Test screen: test [fill|bars|grid|circle|quad]",
          .func = &cmd_test },
        { .command = "next", .help = "Next test screen", .func = &cmd_next },
        { .command = "gauge", .help = "List or select a gauge: gauge [id]", .func = &cmd_gauge },
        { .command = "demo", .help = "Simulator: demo [on|off|sweep]", .func = &cmd_demo },
        { .command = "value", .help = "Drive the needle: value <number>", .func = &cmd_value },
        { .command = "fps", .help = "Frame rate: fps [on|off]", .func = &cmd_fps },
        { .command = "backlight", .help = "Backlight: backlight [0-100]", .func = &cmd_backlight },
        { .command = "touch", .help = "Touch state: touch [watch [seconds]]", .func = &cmd_touch },
        { .command = "flush", .help = "Show panel transfer statistics", .func = &cmd_flush },
        { .command = "free", .help = "Show heap usage", .func = &cmd_free },
        { .command = "version", .help = "Show build information", .func = &cmd_version },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_register_help_command());

    err = esp_console_start_repl(repl);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "console ready on UART0 @ 115200 (try `help`)");
    }
    return err;
}
