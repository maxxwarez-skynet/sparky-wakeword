#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

static const char *TAG = "SPARKY";

void app_main(void)
{
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "      SPARKY FIRMWARE START     ");
    ESP_LOGI(TAG, "================================");

    ESP_ERROR_CHECK(
        sparky_display_init()
    );

    ESP_ERROR_CHECK(
        sparky_display_test()
    );

    ESP_LOGI(TAG, "LCD test completed");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}