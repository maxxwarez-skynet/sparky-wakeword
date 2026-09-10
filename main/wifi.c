#include "wifi.h"

#include <string.h>
#include <stdio.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "SPARKY_WIFI";
static bool s_connected;
static bool s_initialized;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to configured AP");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected, retrying");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        s_connected = true;
        ESP_LOGI(TAG, "Connected");
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t sparky_wifi_init(void)
{
    if (s_initialized) return ESP_OK;
    if (CONFIG_SPARKY_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "WiFi SSID is unset; configure Sparky Configuration in menuconfig");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;
    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_FAIL;

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                        wifi_event_handler, NULL, NULL), TAG, "WiFi event registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                        wifi_event_handler, NULL, NULL), TAG, "IP event registration failed");

    wifi_config_t config = { 0 };
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", CONFIG_SPARKY_WIFI_SSID);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s", CONFIG_SPARKY_WIFI_PASSWORD);
    config.sta.threshold.authmode = CONFIG_SPARKY_WIFI_PASSWORD[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "Failed to configure station");
    ESP_LOGI(TAG, "Starting WiFi STA");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");
    s_initialized = true;
    return ESP_OK;
}

bool sparky_wifi_is_connected(void)
{
    return s_connected;
}
