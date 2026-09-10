idf_component_register(
    SRCS
        "sparky-wakeword.c"
        "display.c"
        "touch.c"
        "audio.c"
        "i2c_bus.c"
        "afe.c"
        "speech_capture.c"
        "talk_client.c"
        "wifi.c"
       

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_spi
        esp_lcd
        esp_http_client
        esp_netif
        esp_wifi
        mbedtls
        nvs_flash
        espressif__cjson
        espressif__esp_codec_dev
        espressif__esp-sr
)
