/*
 * bsp.c - ESP32-2432S028R ("CYD") board support, no graphics library.
 *
 *   ESP32-D0WD-V3, 4 MB flash, 2.8" 320x240 IPS panel, XPT2046 resistive touch
 *
 *   LCD   SCK GPIO14   MOSI GPIO13   MISO GPIO12   CS GPIO15
 *         DC  GPIO2    RST  (EN)     BL   GPIO21
 *   Touch SCK GPIO25   MOSI GPIO32   MISO GPIO39   CS GPIO33   IRQ GPIO36
 *
 * The panel is a 240x320 portrait part; it is brought up in 320x240 landscape
 * (swap_xy).  The two common controller variants of this board (ILI9341 and
 * ST7789V) are both supported - pick one under menuconfig -> Gauge BSP.
 *
 * The initialisation follows the same philosophy as the round board's GC9A01A
 * bring-up: the vendor's/IDF's table is used as-is, and colour inversion is
 * only ever *turned on* by us, never cancelled with an explicit INVOFF - see
 * docs/hardware.md for what that cost us last time.
 */
#include "bsp.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_BSP_PANEL_ILI9341
#include "esp_lcd_ili9341.h"
#else
#include "esp_lcd_panel_st7789.h"
#endif
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#define TAG "bsp"

#define LCD_HOST     ((spi_host_device_t)CONFIG_BSP_LCD_SPI_HOST)
#define LCD_PIN_SCK  CONFIG_BSP_LCD_PIN_SCK
#define LCD_PIN_MOSI CONFIG_BSP_LCD_PIN_MOSI
#define LCD_PIN_MISO CONFIG_BSP_LCD_PIN_MISO
#define LCD_PIN_CS   CONFIG_BSP_LCD_PIN_CS
#define LCD_PIN_DC   CONFIG_BSP_LCD_PIN_DC
#define LCD_PIN_RST  CONFIG_BSP_LCD_PIN_RST
#define LCD_PIN_BL   CONFIG_BSP_LCD_PIN_BL

#define LCD_CLK_HZ   (CONFIG_BSP_LCD_SPI_CLK_MHZ * 1000 * 1000)

/* -------------------------------------------------------------------------- */

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_flush_done;
static int s_backlight = -1;
static volatile uint32_t s_flush_count;

/* Framebuffers are little-endian RGB565; the panel wants the high byte of each
 * pixel first. */
static inline void swap_rgb565(uint16_t *px, size_t count)
{
#if CONFIG_BSP_LCD_SWAP_RGB565_BYTES
    /* Two pixels per store.  Byte order inside each 16-bit pixel flips, and a
     * 32-bit word holds exactly two pixels on a little-endian CPU. */
    size_t i = 0;
    if (((uintptr_t)px & 3u) && count > 0) {
        px[0] = (uint16_t)((px[0] >> 8) | (px[0] << 8));
        i = 1;
    }
    uint32_t *pairs = (uint32_t *)(px + i);
    const size_t n = (count - i) / 2;
    for (size_t j = 0; j < n; j++) {
        const uint32_t v = pairs[j];
        pairs[j] = ((v & 0xFF00FF00u) >> 8) | ((v & 0x00FF00FFu) << 8);
    }
    i += n * 2;
    if (i < count) {
        px[i] = (uint16_t)((px[i] >> 8) | (px[i] << 8));
    }
#else
    (void)px;
    (void)count;
#endif
}

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    BaseType_t woken = pdFALSE;
    if (s_flush_done) {
        xSemaphoreGiveFromISR(s_flush_done, &woken);
    }
    return woken == pdTRUE;
}

esp_err_t bsp_lcd_draw_bitmap(int x0, int y0, int x1, int y1, const uint16_t *pixels)
{
    if (!s_panel || !pixels || x1 <= x0 || y1 <= y0) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t count = (size_t)(x1 - x0) * (size_t)(y1 - y0);
    swap_rgb565((uint16_t *)pixels, count);

    /* Drain a stale completion before starting a new transfer. */
    if (s_flush_done) {
        xSemaphoreTake(s_flush_done, 0);
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x1, y1, pixels);
    if (err != ESP_OK) {
        return err;
    }

    if (s_flush_done && xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_flush_count++;
    return ESP_OK;
}

esp_err_t bsp_lcd_fill_rect(int x0, int y0, int x1, int y1, uint16_t colour)
{
    static uint16_t line[BSP_LCD_H_RES];

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > BSP_LCD_H_RES) x1 = BSP_LCD_H_RES;
    if (y1 > BSP_LCD_V_RES) y1 = BSP_LCD_V_RES;
    if (x1 <= x0 || y1 <= y0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int x = x0; x < x1; x++) {
        line[x] = colour;
    }

    for (int y = y0; y < y1; y++) {
        esp_err_t err = bsp_lcd_draw_bitmap(x0, y, x1, y + 1, line + x0);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

uint32_t bsp_lcd_flush_count(void)
{
    return s_flush_count;
}

/* -------------------------------------------------------------------------- */
/* backlight                                                                  */
/* -------------------------------------------------------------------------- */

void bsp_backlight_set(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_backlight = percent;
#if LCD_PIN_BL >= 0
    uint32_t duty = (uint32_t)((1023 * percent) / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
}

int bsp_backlight_get(void)
{
    return s_backlight;
}

static esp_err_t backlight_init(void)
{
#if LCD_PIN_BL >= 0
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

    ledc_channel_config_t ch = {
        .gpio_num = LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "ledc channel");
    bsp_backlight_set(CONFIG_BSP_BACKLIGHT_DEFAULT_PERCENT);
#else
    s_backlight = 0;
#endif
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* panel                                                                      */
/* -------------------------------------------------------------------------- */

esp_err_t bsp_display_init(void)
{
    if (s_panel) {
        return ESP_OK;
    }

    s_flush_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_done, ESP_ERR_NO_MEM, TAG, "flush semaphore");

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_PIN_SCK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_CLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io),
                        TAG, "panel io");

    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = on_color_trans_done };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(io, &cbs, NULL), TAG, "io callbacks");

    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        /*
         * Element order is a property of the panel wiring, not the controller:
         * get it wrong and red and blue swap while shapes stay perfect.  The
         * verified values for this board are in Kconfig / sdkconfig.defaults.
         */
#ifdef CONFIG_BSP_LCD_RGB_ORDER_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
#if CONFIG_BSP_PANEL_ILI9341
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &dev_cfg, &s_panel), TAG, "ili9341");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &dev_cfg, &s_panel), TAG, "st7789");
#endif

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");

    /*
     * Only ever *enable* inversion.  If the image comes out as a negative, set
     * BSP_LCD_INVERT_COLOR; never call esp_lcd_panel_invert_color(panel, false)
     * to "fix" a panel whose own init sequence already ends in INVON - that
     * sends INVOFF and cancels it.
     */
#if CONFIG_BSP_LCD_INVERT_COLOR
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert colour");
#endif

    /* Landscape: the native addressing is 240x320 portrait. */
#if CONFIG_BSP_LCD_SWAP_XY
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "swap xy");
#endif
    /* Booleans that default to "n" are absent from sdkconfig.h, so they cannot
     * be passed to the API directly - turn them into real 0/1 values here. */
#ifdef CONFIG_BSP_LCD_MIRROR_X
    const bool mirror_x = true;
#else
    const bool mirror_x = false;
#endif
#ifdef CONFIG_BSP_LCD_MIRROR_Y
    const bool mirror_y = true;
#else
    const bool mirror_y = false;
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, mirror_x, mirror_y), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

#if CONFIG_BSP_PANEL_ILI9341
    const char *panel_name = "ILI9341";
#else
    const char *panel_name = "ST7789V";
#endif
    ESP_LOGI(TAG, "%s up: %dx%d landscape @ %d MHz SPI",
             panel_name, BSP_LCD_H_RES, BSP_LCD_V_RES, CONFIG_BSP_LCD_SPI_CLK_MHZ);
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_lcd_panel(void)
{
    return s_panel;
}

/* -------------------------------------------------------------------------- */
/* touch - XPT2046 on its own SPI bus                                         */
/* -------------------------------------------------------------------------- */

#define TOUCH_HOST   ((spi_host_device_t)CONFIG_BSP_TOUCH_SPI_HOST)
#define TOUCH_CLK_HZ (CONFIG_BSP_TOUCH_SPI_CLK_KHZ * 1000)

#define XPT_CMD_X    0xD0u   /* start | channel 5 | 12-bit | differential */
#define XPT_CMD_Y    0x90u   /* start | channel 1 | 12-bit | differential */

static spi_device_handle_t s_touch;
static int s_last_x;
static int s_last_y;
static bool s_last_pressed;

static uint16_t xpt_read(uint8_t cmd)
{
    /* Full duplex: control byte, then the chip shifts its 12-bit result out on
     * the following clocks.  The sample sits in bytes 1..2, right-aligned. */
    uint8_t tx[3] = {cmd, 0x00, 0x00};
    uint8_t rx[3] = {0x00, 0x00, 0x00};
    spi_transaction_t t = {
        .length = 3 * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_polling_transmit(s_touch, &t) != ESP_OK) {
        return 0;
    }
    const uint16_t v = (uint16_t)(((rx[1] << 8) | rx[2]) >> 3);
    return v & 0x0FFF;
}

static int xpt_read_avg(uint8_t cmd)
{
    int sum = 0;
    int n = 0;
    /* the first conversion after an idle period can be stale */
    (void)xpt_read(cmd);
    for (int i = 0; i < 4; i++) {
        const int v = xpt_read(cmd);
        if (v > 0) {
            sum += v;
            n++;
        }
    }
    return n ? (sum + n / 2) / n : 0;
}

bool bsp_touch_irq_active(void)
{
#if CONFIG_BSP_TOUCH_PIN_IRQ >= 0
    return gpio_get_level((gpio_num_t)CONFIG_BSP_TOUCH_PIN_IRQ) == 0;
#else
    return true;
#endif
}

static bool touch_sample(int *x, int *y)
{
    const int xs = xpt_read_avg(XPT_CMD_X);
    const int ys = xpt_read_avg(XPT_CMD_Y);

    s_last_x = xs;
    s_last_y = ys;

    const bool in_range = xs > CONFIG_BSP_TOUCH_RAW_MIN && xs < CONFIG_BSP_TOUCH_RAW_MAX &&
                          ys > CONFIG_BSP_TOUCH_RAW_MIN && ys < CONFIG_BSP_TOUCH_RAW_MAX;
#if CONFIG_BSP_TOUCH_PIN_IRQ >= 0
    const bool pressed = in_range && bsp_touch_irq_active();
#else
    const bool pressed = in_range;
#endif
    s_last_pressed = pressed;
    if (pressed) {
        if (x) *x = xs;
        if (y) *y = ys;
    }
    return pressed;
}

bool bsp_touch_read_raw(int *x, int *y)
{
    if (!s_touch) {
        return false;
    }
    return touch_sample(x, y);
}

void bsp_touch_last_raw(int *x, int *y, bool *pressed)
{
    if (x) *x = s_last_x;
    if (y) *y = s_last_y;
    if (pressed) *pressed = s_last_pressed;
}

esp_err_t bsp_touch_init(void)
{
    if (s_touch) {
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = CONFIG_BSP_TOUCH_PIN_SCK,
        .mosi_io_num = CONFIG_BSP_TOUCH_PIN_MOSI,
        .miso_io_num = CONFIG_BSP_TOUCH_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8,
    };
    /* 3-byte transfers do not need DMA. */
    ESP_RETURN_ON_ERROR(spi_bus_initialize(TOUCH_HOST, &bus_cfg, SPI_DMA_DISABLED), TAG, "touch bus");

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = TOUCH_CLK_HZ,
        .mode = 0,
        .spics_io_num = CONFIG_BSP_TOUCH_PIN_CS,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(TOUCH_HOST, &dev_cfg, &s_touch), TAG, "touch device");

#if CONFIG_BSP_TOUCH_PIN_IRQ >= 0
    gpio_config_t irq = {
        .pin_bit_mask = 1ULL << CONFIG_BSP_TOUCH_PIN_IRQ,
        .mode = GPIO_MODE_INPUT,
        /* GPIO34-39 are input-only pads without internal pull-ups. */
        .pull_up_en = (CONFIG_BSP_TOUCH_PIN_IRQ < 34) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&irq), TAG, "touch irq");
#endif

    ESP_LOGI(TAG, "XPT2046 up on SPI%d @ %d kHz (IRQ %d)",
             (int)CONFIG_BSP_TOUCH_SPI_HOST, (int)CONFIG_BSP_TOUCH_SPI_CLK_KHZ,
             (int)CONFIG_BSP_TOUCH_PIN_IRQ);
    return ESP_OK;
}