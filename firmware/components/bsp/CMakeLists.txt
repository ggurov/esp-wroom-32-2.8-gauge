idf_component_register(
    SRCS "bsp.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc freertos
    # The controller drivers are only used inside bsp.c, and only the selected
    # one is linked in.
    PRIV_REQUIRES espressif__esp_lcd_ili9341
)