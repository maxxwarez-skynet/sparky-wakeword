#include "touch.h"
#include "board.h"
#include "i2c_bus.h"

#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SPARKY_TOUCH";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_touch = NULL;


/*
 * CST816 registers
 *
 * 0x02:
 *   low nibble = number of active touch points
 *
 * 0x03:
 *   X high byte, low nibble significant
 *
 * 0x04:
 *   X low byte
 *
 * 0x05:
 *   Y high byte, low nibble significant
 *
 * 0x06:
 *   Y low byte
 */
#define CST816_REG_TOUCH_DATA       0x02
#define CST816_REG_CHIP_ID          0xA7
#define CST816_REG_AUTO_SLEEP       0xFE


static esp_err_t cst816_write_register(
    uint8_t reg,
    uint8_t value
)
{
    uint8_t data[2] = {
        reg,
        value
    };

    return i2c_master_transmit(
        s_touch,
        data,
        sizeof(data),
        100
    );
}


static esp_err_t cst816_read_register(
    uint8_t reg,
    uint8_t *value
)
{
    return i2c_master_transmit_receive(
        s_touch,
        &reg,
        1,
        value,
        1,
        100
    );
}


esp_err_t sparky_touch_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing CST816T");


    /*
     * Hardware reset.
     */
    gpio_config_t rst_config = {
        .pin_bit_mask = 1ULL << SPARKY_TOUCH_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ret = gpio_config(&rst_config);

    if (ret != ESP_OK) {
        return ret;
    }

    gpio_set_level(
        SPARKY_TOUCH_RST_GPIO,
        0
    );

    vTaskDelay(
        pdMS_TO_TICKS(10)
    );

    gpio_set_level(
        SPARKY_TOUCH_RST_GPIO,
        1
    );

    vTaskDelay(
        pdMS_TO_TICKS(100)
    );


    /*
     * Get the shared I2C master bus.
     */
    ESP_LOGI(
        TAG,
        "I2C SDA=%d SCL=%d",
        SPARKY_TOUCH_SDA_GPIO,
        SPARKY_TOUCH_SCL_GPIO
    );

    ret = sparky_i2c_init();

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Shared I2C bus initialization failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }

    s_i2c_bus = sparky_i2c_get_bus();


    /*
     * Add CST816T device to the shared bus.
     */
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,

        .device_address = SPARKY_TOUCH_I2C_ADDR,

        .scl_speed_hz = SPARKY_TOUCH_I2C_SPEED_HZ,
    };

    ret = i2c_master_bus_add_device(
        s_i2c_bus,
        &dev_config,
        &s_touch
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to add CST816 device: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    /*
     * Verify that the controller responds.
     */
    uint8_t chip_id = 0;

    ret = cst816_read_register(
        CST816_REG_CHIP_ID,
        &chip_id
    );

    if (ret != ESP_OK) {

        ESP_LOGE(
            TAG,
            "CST816 ID read failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }

    ESP_LOGI(
        TAG,
        "CST816 detected: ID=0x%02X address=0x%02X",
        chip_id,
        SPARKY_TOUCH_I2C_ADDR
    );


    /*
     * Prevent the controller from automatically sleeping.
     *
     * This is useful for our initial polling test.
     */
    ret = cst816_write_register(
        CST816_REG_AUTO_SLEEP,
        0x01
    );

    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Could not configure CST816 sleep mode: %s",
            esp_err_to_name(ret)
        );
    }


    ESP_LOGI(TAG, "CST816T initialized successfully");

    return ESP_OK;
}


esp_err_t sparky_touch_read(
    uint16_t *x,
    uint16_t *y,
    bool *pressed
)
{
    if (
        s_touch == NULL ||
        x == NULL ||
        y == NULL ||
        pressed == NULL
    ) {
        return ESP_ERR_INVALID_STATE;
    }


    uint8_t reg = CST816_REG_TOUCH_DATA;

    uint8_t data[5] = {0};


    esp_err_t ret = i2c_master_transmit_receive(
        s_touch,
        &reg,
        1,
        data,
        sizeof(data),
        100
    );

    if (ret != ESP_OK) {
        return ret;
    }


    /*
     * Low nibble contains the number of touch points.
     */
    uint8_t fingers = data[0] & 0x0F;


    if (fingers == 0) {
        *pressed = false;
        return ESP_OK;
    }


    /*
     * CST816 coordinate format:
     *
     * X = 12-bit
     * Y = 12-bit
     */
    *x =
        ((uint16_t)(data[1] & 0x0F) << 8) |
        data[2];

    *y =
        ((uint16_t)(data[3] & 0x0F) << 8) |
        data[4];

    *pressed = true;

    return ESP_OK;
}