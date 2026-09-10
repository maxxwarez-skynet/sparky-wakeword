#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>

#include "display.h"
#include "touch.h"

#include "audio.h"

#include "afe.h"

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


    /*
     * Initialize audio input.
     */
    ESP_ERROR_CHECK(
        sparky_audio_init()
    );


    /*
     * Initialize ESP-SR AFE.
     */
    ESP_ERROR_CHECK(
        sparky_afe_init()
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


    /*
     * ESP-SR currently reports:
     *
     *   Feed chunk    = 1024 samples/channel
     *   Feed channels = 2
     *
     * Therefore one complete feed buffer is:
     *
     *   1024 * 2 = 2048 int16_t samples
     */
    int16_t afe_samples[2048];


    uint16_t x = 0;
    uint16_t y = 0;

    bool pressed = false;
    bool previous_pressed = false;


    while (1) {

        /*
         * ----------------------------------------------------
         * Touch
         * ----------------------------------------------------
         */

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

                sparky_display_touch_marker(
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
         * ----------------------------------------------------
         * Audio -> ESP-SR AFE
         * ----------------------------------------------------
         *
         * Read one complete AFE input frame from I2S.
         */
        ret = sparky_audio_read(
            afe_samples,
            2048
        );


        if (ret != ESP_OK) {

            ESP_LOGE(
                TAG,
                "Audio read failed: %s",
                esp_err_to_name(ret)
            );

        } else {

            /*
             * Feed the microphone samples into ESP-SR.
             */
            ret = sparky_afe_feed(
                afe_samples,
                2048
            );


            if (ret != ESP_OK) {

                ESP_LOGE(
                    TAG,
                    "AFE feed failed: %s",
                    esp_err_to_name(ret)
                );
            } else {

                /*
                 * One 1024-sample input frame produces two 512-sample
                 * output frames.  Consume both so the AFE ring buffer
                 * remains balanced.
                */
                for (int i = 0; i < 2; i++) {
                    ret = sparky_afe_fetch();

                    if (ret != ESP_OK) {
                        ESP_LOGE(
                            TAG,
                            "AFE fetch failed: %s",
                            esp_err_to_name(ret)
                        );
                        break;
                    }

                    if (sparky_afe_wake_word_detected()) {
                        ESP_LOGI(TAG, ">>> SPARKY WAKE EVENT <<<");

                        ret = sparky_display_awake();
                        if (ret != ESP_OK) {
                            ESP_LOGE(
                                TAG,
                                "Failed to show AWAKE state: %s",
                                esp_err_to_name(ret)
                            );
                        }
                    }
                }
            }
        }


        /*
         * No audio test here anymore.
         *
         * The audio is now being consumed by
         * the ESP-SR AFE instead.
         */


        /*
         * 50 ms polling interval.
         */
        // vTaskDelay(
        //     pdMS_TO_TICKS(50)
        // );
    }
}
