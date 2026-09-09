#include "display.h"
#include "board.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

static const char *TAG = "SPARKY_DISPLAY";

static esp_lcd_panel_handle_t s_panel = NULL;


esp_err_t sparky_display_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing backlight");

    gpio_config_t bl_config = {
        .pin_bit_mask = 1ULL << SPARKY_LCD_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ret = gpio_config(&bl_config);
    if (ret != ESP_OK) {
        return ret;
    }

    gpio_set_level(SPARKY_LCD_BL_GPIO, 0);


    ESP_LOGI(TAG, "Initializing SPI bus");

    spi_bus_config_t bus_config = {
        .sclk_io_num = SPARKY_LCD_SCLK_GPIO,
        .mosi_io_num = SPARKY_LCD_MOSI_GPIO,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,

        .max_transfer_sz =
            SPARKY_LCD_H_RES *
            50 *
            sizeof(uint16_t),
    };

    ret = spi_bus_initialize(
        SPARKY_LCD_HOST,
        &bus_config,
        SPI_DMA_CH_AUTO
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI initialization failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }


    ESP_LOGI(TAG, "Initializing ST7789 SPI interface");

    esp_lcd_panel_io_handle_t io_handle = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = SPARKY_LCD_DC_GPIO,
        .cs_gpio_num = SPARKY_LCD_CS_GPIO,

        .pclk_hz = SPARKY_LCD_PIXEL_CLOCK_HZ,

        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,

        .spi_mode = 0,

        .trans_queue_depth = 10,
    };

    ret = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPARKY_LCD_HOST,
        &io_config,
        &io_handle
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD IO initialization failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }


    ESP_LOGI(TAG, "Creating ST7789 panel");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = SPARKY_LCD_RST_GPIO,
        .bits_per_pixel = 16,
    };

    ret = esp_lcd_new_panel_st7789(
        io_handle,
        &panel_config,
        &s_panel
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ST7789 creation failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }


    ESP_LOGI(TAG, "Resetting display");

    ret = esp_lcd_panel_reset(s_panel);
    if (ret != ESP_OK) {
        return ret;
    }


    ESP_LOGI(TAG, "Initializing display");

    ret = esp_lcd_panel_init(s_panel);
    if (ret != ESP_OK) {
        return ret;
    }


    /*
     * IMPORTANT:
     *
     * Do NOT set a panel gap here.
     *
     * Waveshare's official example for this exact
     * board does not set one.
     */


    ESP_LOGI(TAG, "Enabling color inversion");

    ret = esp_lcd_panel_invert_color(
        s_panel,
        true
    );

    if (ret != ESP_OK) {
        return ret;
    }


    ESP_LOGI(TAG, "Turning display on");

    ret = esp_lcd_panel_disp_on_off(
        s_panel,
        true
    );

    if (ret != ESP_OK) {
        return ret;
    }


    gpio_set_level(
        SPARKY_LCD_BL_GPIO,
        1
    );

    ESP_LOGI(TAG, "ST7789 initialized successfully");

    return ESP_OK;
}


esp_err_t sparky_display_test(void)
{
    if (s_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * RGB565 green:
     *
     * R = 0
     * G = 63
     * B = 0
     *
     * 0x07E0
     *
     * Waveshare's LVGL configuration enables
     * RGB565 byte swapping, so send the bytes
     * explicitly in display order.
     */

    const uint8_t color_hi = 0x07;
    const uint8_t color_lo = 0xE0;

    uint8_t *buffer = heap_caps_malloc(
        SPARKY_LCD_H_RES * 2,
        MALLOC_CAP_DMA
    );

    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }


    for (int x = 0; x < SPARKY_LCD_H_RES; x++) {
        buffer[x * 2 + 0] = color_hi;
        buffer[x * 2 + 1] = color_lo;
    }


    for (int y = 0; y < SPARKY_LCD_V_RES; y++) {

        esp_err_t ret = esp_lcd_panel_draw_bitmap(
            s_panel,
            0,
            y,
            SPARKY_LCD_H_RES,
            y + 1,
            buffer
        );

        if (ret != ESP_OK) {
            free(buffer);
            return ret;
        }
    }


    free(buffer);

    ESP_LOGI(TAG, "LCD test pattern drawn");

    return ESP_OK;
}