#include "afe.h"

#include <string.h>

#include "esp_log.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_models.h"
#include "model_path.h"


static const char *TAG = "SPARKY_AFE";


static srmodel_list_t *s_models = NULL;

static const esp_afe_sr_iface_t *s_afe_handle = NULL;

static esp_afe_sr_data_t *s_afe_data = NULL;


/*
 * AFE initialization
 *
 * Hardware input:
 *
 *   ES7210
 *      |
 *      +-- microphone 1
 *      |
 *      +-- microphone 2
 *      |
 *      v
 *   I2S stereo / 16 kHz / 16-bit
 *
 * ESP-SR input format:
 *
 *   MM
 *
 * M = microphone
 * M = microphone
 */
esp_err_t sparky_afe_init(void)
{
    ESP_LOGI(
        TAG,
        "Initializing ESP-SR AFE"
    );


    /*
     * Load the models from the ESP-SR model partition.
     */
    s_models = esp_srmodel_init("model");

    if (s_models == NULL) {

        ESP_LOGE(
            TAG,
            "Failed to initialize ESP-SR model partition"
        );

        return ESP_FAIL;
    }


    ESP_LOGI(
        TAG,
        "ESP-SR models loaded: %d",
        s_models->num
    );


    /*
     * Print the WakeNet models that are actually
     * present in flash.
     */
    for (int i = 0; i < s_models->num; i++) {

        if (s_models->model_name[i] == NULL) {
            continue;
        }


        if (strstr(
                s_models->model_name[i],
                ESP_WN_PREFIX
            ) != NULL) {

            ESP_LOGI(
                TAG,
                "WakeNet model: %s",
                s_models->model_name[i]
            );
        }
    }


    /*
     * Create the default AFE configuration.
     *
     * "MM" means:
     *
     *   channel 0 = microphone
     *   channel 1 = microphone
     *
     * No playback-reference channel is present.
     */
    afe_config_t *afe_config =
        afe_config_init(
            "MM",
            s_models,
            AFE_TYPE_SR,
            AFE_MODE_HIGH_PERF
        );


    if (afe_config == NULL) {

        ESP_LOGE(
            TAG,
            "afe_config_init() failed"
        );

        return ESP_FAIL;
    }


    /*
     * Print the model selected by the AFE configuration.
     */
    if (afe_config->wakenet_model_name != NULL) {

        ESP_LOGI(
            TAG,
            "WakeNet model 1: %s",
            afe_config->wakenet_model_name
        );

    } else {

        ESP_LOGW(
            TAG,
            "No WakeNet model selected"
        );
    }


    if (afe_config->wakenet_model_name_2 != NULL) {

        ESP_LOGI(
            TAG,
            "WakeNet model 2: %s",
            afe_config->wakenet_model_name_2
        );
    }


    /*
     * Let ESP-SR validate and resolve any configuration
     * conflicts.
     */
    afe_config =
        afe_config_check(afe_config);


    if (afe_config == NULL) {

        ESP_LOGE(
            TAG,
            "afe_config_check() failed"
        );

        return ESP_FAIL;
    }


    /*
     * Print the final AFE configuration.
     */
    afe_config_print(afe_config);


    /*
     * Obtain the ESP32-S3 AFE interface.
     */
    s_afe_handle =
        esp_afe_handle_from_config(
            afe_config
        );


    if (s_afe_handle == NULL) {

        ESP_LOGE(
            TAG,
            "esp_afe_handle_from_config() failed"
        );

        afe_config_free(afe_config);

        return ESP_FAIL;
    }


    /*
     * Create the actual AFE instance.
     */
    s_afe_data =
        s_afe_handle->create_from_config(
            afe_config
        );


    /*
     * Configuration is no longer needed after the
     * AFE instance has been created.
     */
    afe_config_free(
        afe_config
    );


    if (s_afe_data == NULL) {

        ESP_LOGE(
            TAG,
            "AFE create_from_config() failed"
        );

        return ESP_FAIL;
    }


    /*
     * Query the actual parameters selected by
     * ESP-SR.
     */
    int feed_chunk =
        s_afe_handle->get_feed_chunksize(
            s_afe_data
        );

    int fetch_chunk =
        s_afe_handle->get_fetch_chunksize(
            s_afe_data
        );

    int feed_channels =
        s_afe_handle->get_feed_channel_num(
            s_afe_data
        );

    int fetch_channels =
        s_afe_handle->get_fetch_channel_num(
            s_afe_data
        );

    int sample_rate =
        s_afe_handle->get_samp_rate(
            s_afe_data
        );


    ESP_LOGI(
        TAG,
        "--------------------------------"
    );

    ESP_LOGI(
        TAG,
        "AFE initialized successfully"
    );

    ESP_LOGI(
        TAG,
        "Sample rate:      %d Hz",
        sample_rate
    );

    ESP_LOGI(
        TAG,
        "Feed channels:    %d",
        feed_channels
    );

    ESP_LOGI(
        TAG,
        "Feed chunk:       %d samples/channel",
        feed_chunk
    );

    ESP_LOGI(
        TAG,
        "Fetch channels:   %d",
        fetch_channels
    );

    ESP_LOGI(
        TAG,
        "Fetch chunk:      %d samples",
        fetch_chunk
    );

    ESP_LOGI(
        TAG,
        "--------------------------------"
    );


    /*
     * Print the actual algorithm pipeline.
     */
    s_afe_handle->print_pipeline(
        s_afe_data
    );


    return ESP_OK;
}


/*
 * Feed microphone samples into ESP-SR.
 *
 * The caller must provide interleaved signed 16-bit PCM:
 *
 *   mic0, mic1, mic0, mic1, ...
 *
 * For the current configuration:
 *
 *   1024 samples/channel
 *   2 channels
 *
 * therefore one complete AFE feed buffer contains:
 *
 *   2048 int16_t samples
 */
esp_err_t sparky_afe_feed(
    const int16_t *samples,
    size_t sample_count
)

{
    if (s_afe_handle == NULL ||
        s_afe_data == NULL) {

        return ESP_ERR_INVALID_STATE;
    }


    if (samples == NULL ||
        sample_count == 0) {

        return ESP_ERR_INVALID_ARG;
    }


    int feed_chunk =
        s_afe_handle->get_feed_chunksize(
            s_afe_data
        );

    int feed_channels =
        s_afe_handle->get_feed_channel_num(
            s_afe_data
        );


    size_t expected_samples =
        (size_t)feed_chunk *
        (size_t)feed_channels;


    if (sample_count != expected_samples) {

        ESP_LOGE(
            TAG,
            "Invalid AFE feed size: got %u samples, expected %u",
            (unsigned)sample_count,
            (unsigned)expected_samples
        );

        return ESP_ERR_INVALID_SIZE;
    }


    int ret = s_afe_handle->feed(
        s_afe_data,
        samples
    );

if (ret < 0) {
    ESP_LOGE(
        TAG,
        "AFE feed failed: %d",
        ret
    );

    return ESP_FAIL;
}

return ESP_OK;

    return ESP_OK;

}

/*
 * Fetch one processed result from ESP-SR.
 *
 * This is only a diagnostic step.
 * We are not acting on the wake-word state yet.
 */
esp_err_t sparky_afe_fetch(void)
{
    if (s_afe_handle == NULL ||
        s_afe_data == NULL) {

        return ESP_ERR_INVALID_STATE;
    }


    afe_fetch_result_t *result =
        s_afe_handle->fetch(
            s_afe_data
        );


    if (result == NULL) {

        ESP_LOGW(
            TAG,
            "AFE fetch returned NULL"
        );

        return ESP_FAIL;
    }


    ESP_LOGI(
        TAG,
        "AFE: ret=%d vad=%d wakeup=%d wake_word=%d volume=%d",
        result->ret_value,
        result->vad_state,
        result->wakeup_state,
        result->wake_word_index,
        result->data_volume
    );


    return ESP_OK;
}