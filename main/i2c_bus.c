#include "i2c_bus.h"
#include "board.h"

#include "esp_log.h"

static const char *TAG = "SPARKY_I2C";

static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t sparky_i2c_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }

    ESP_LOGI(
        TAG,
        "Initializing shared I2C bus"
    );

    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SPARKY_TOUCH_SDA_GPIO,
        .scl_io_num = SPARKY_TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(
        &config,
        &s_bus
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2C initialization failed: %s",
            esp_err_to_name(ret)
        );
        return ret;
    }

    ESP_LOGI(
        TAG,
        "Shared I2C bus ready: SDA=%d SCL=%d",
        SPARKY_TOUCH_SDA_GPIO,
        SPARKY_TOUCH_SCL_GPIO
    );

    return ESP_OK;
}

i2c_master_bus_handle_t sparky_i2c_get_bus(void)
{
    return s_bus;
}