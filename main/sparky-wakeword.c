#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "touch.h"

static const char *TAG = "SPARKY";


void app_main(void)
{
    ESP_LOGI(
        TAG,
        "================================"
    );

    ESP_LOGI(
        TAG,
        "      SPARKY FIRMWARE START"
    );

    ESP_LOGI(
        TAG,
        "================================"
    );


    /*
     * Milestone 1:
     * Initialize LCD.
     */
    ESP_ERROR_CHECK(
        sparky_display_init()
    );

    ESP_ERROR_CHECK(
        sparky_display_test()
    );

    ESP_LOGI(
        TAG,
        "LCD test completed"
    );


    /*
     * Milestone 2:
     * Initialize CST816T.
     */
    ESP_ERROR_CHECK(
        sparky_touch_init()
    );


    ESP_LOGI(
        TAG,
        "Touch test running..."
    );


    uint16_t x = 0;
    uint16_t y = 0;

    bool pressed = false;
    bool previous_pressed = false;


    while (1) {

        esp_err_t ret = sparky_touch_read(
            &x,
            &y,
            &pressed
        );


        if (ret != ESP_OK) {

            ESP_LOGE(
                TAG,
                "Touch read failed: %s",
                esp_err_to_name(ret)
            );

        } else {

            /*
             * Print only on state changes or
             * while a finger is actively moving.
             */
            if (pressed) {

                ESP_LOGI(
                    TAG,
                    "TOUCH: x=%u y=%u",
                    x,
                    y
                );

            } else if (previous_pressed) {

                ESP_LOGI(
                    TAG,
                    "TOUCH: RELEASE"
                );
            }
        }


        previous_pressed = pressed;


        /*
         * 50 ms polling interval.
         *
         * We'll eventually replace this with
         * the CST816 interrupt pin.
         */
        vTaskDelay(
            pdMS_TO_TICKS(50)
        );
    }
}