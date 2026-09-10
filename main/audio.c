#include "audio.h"
#include "board.h"
#include "i2c_bus.h"

#include <math.h>
#include <stdint.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SPARKY_AUDIO";

static i2s_chan_handle_t s_rx_chan = NULL;
static i2s_chan_handle_t s_tx_chan = NULL;
static i2c_master_dev_handle_t s_es7210 = NULL;
static esp_codec_dev_handle_t s_es8311 = NULL;


/* ============================================================
 * ES7210 register definitions
 * ============================================================ */

#define ES7210_REG_RESET              0x00
#define ES7210_REG_MAINCLK            0x02
#define ES7210_REG_LRCK_DIVH          0x04
#define ES7210_REG_LRCK_DIVL          0x05
#define ES7210_REG_POWER_DOWN         0x06
#define ES7210_REG_OSR                0x07

#define ES7210_REG_TIME_CONTROL0      0x09
#define ES7210_REG_TIME_CONTROL1      0x0A

#define ES7210_REG_SDP_INTERFACE1    0x11
#define ES7210_REG_SDP_INTERFACE2    0x12

#define ES7210_REG_ADC1_DB           0x1B
#define ES7210_REG_ADC2_DB           0x1C
#define ES7210_REG_ADC3_DB           0x1D
#define ES7210_REG_ADC4_DB           0x1E

#define ES7210_REG_ADC34_HPF2        0x20
#define ES7210_REG_ADC34_HPF1        0x21
#define ES7210_REG_ADC12_HPF2        0x22
#define ES7210_REG_ADC12_HPF1        0x23

#define ES7210_REG_ANALOG            0x40
#define ES7210_REG_MIC12_BIAS        0x41
#define ES7210_REG_MIC34_BIAS        0x42

#define ES7210_REG_MIC1_GAIN         0x43
#define ES7210_REG_MIC2_GAIN         0x44
#define ES7210_REG_MIC3_GAIN         0x45
#define ES7210_REG_MIC4_GAIN         0x46

#define ES7210_REG_MIC1_POWER        0x47
#define ES7210_REG_MIC2_POWER        0x48
#define ES7210_REG_MIC3_POWER        0x49
#define ES7210_REG_MIC4_POWER        0x4A

#define ES7210_REG_MIC12_POWER       0x4B
#define ES7210_REG_MIC34_POWER       0x4C


/* ============================================================
 * ES7210 I2C helpers
 * ============================================================ */

static esp_err_t es7210_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {
        reg,
        value
    };

    return i2c_master_transmit(
        s_es7210,
        data,
        sizeof(data),
        100
    );
}


static esp_err_t es7210_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(
        s_es7210,
        &reg,
        1,
        value,
        1,
        100
    );
}


/* ============================================================
 * ES7210 configuration
 *
 * This follows the Waveshare/Espressif ES7210 configuration
 * used by the official ESP-SR example for this board.
 * ============================================================ */

static esp_err_t es7210_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing ES7210");


    /*
     * Verify the codec is responding.
     */
    uint8_t value = 0;

    ret = es7210_read_reg(
        ES7210_REG_RESET,
        &value
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "ES7210 not responding: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }

    ESP_LOGI(
        TAG,
        "ES7210 responded, reg00=0x%02X",
        value
    );


    /*
     * Software reset.
     *
     * Official Waveshare driver:
     *   REG00 = 0xFF
     *   REG00 = 0x32
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_RESET,
            0xFF
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_RESET,
            0x32
        )
    );


    /*
     * Power-up timing.
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_TIME_CONTROL0,
            0x30
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_TIME_CONTROL1,
            0x30
        )
    );


    /*
     * ADC high-pass filters.
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_ADC12_HPF1,
            0x2A
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_ADC12_HPF2,
            0x0A
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_ADC34_HPF1,
            0x2A
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_ADC34_HPF2,
            0x0A
        )
    );


    /*
     * I2S format:
     *
     * 0x60 = 16-bit
     * 0x00 = standard I2S
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_SDP_INTERFACE1,
            0x60
        )
    );

    /*
     * TDM disabled.
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_SDP_INTERFACE2,
            0x00
        )
    );


    /*
     * Analog power / VMID.
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_ANALOG,
            0xC3
        )
    );


    /*
     * Microphone bias:
     *
     * 0x70 = 2.87 V
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC12_BIAS,
            0x70
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC34_BIAS,
            0x70
        )
    );


    /*
     * Microphone gain:
     *
     * 10 = 30 dB
     * + 0x10 enables the corresponding gain path.
     *
     * Result = 0x1A
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC1_GAIN,
            0x1A
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC2_GAIN,
            0x1A
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC3_GAIN,
            0x1A
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC4_GAIN,
            0x1A
        )
    );


    /*
     * Power on MIC1-4.
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC1_POWER,
            0x08
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC2_POWER,
            0x08
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC3_POWER,
            0x08
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC4_POWER,
            0x08
        )
    );


    /*
     * --------------------------------------------------------
     * 16 kHz / 4.096 MHz MCLK
     *
     * MCLK = 16,000 * 256 = 4,096,000 Hz
     *
     * Official ES7210 coefficient table:
     *
     *   ADC divider = 0x01
     *   DLL         = enabled
     *   doubler     = enabled
     *   OSR         = 0x20
     *   LRCK H      = 0x01
     *   LRCK L      = 0x00
     * --------------------------------------------------------
     */

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_OSR,
            0x20
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MAINCLK,
            0xC1
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_LRCK_DIVH,
            0x01
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_LRCK_DIVL,
            0x00
        )
    );


    /*
     * Power down DLL.
     *
     * This is part of the official initialization sequence.
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_POWER_DOWN,
            0x04
        )
    );


    /*
     * THIS IS THE IMPORTANT PART:
     *
     * Power on MIC bias + ADC + PGA for all channels.
     *
     * 0x0F = all four channels enabled.
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC12_POWER,
            0x0F
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_MIC34_POWER,
            0x0F
        )
    );


    /*
     * Enable ES7210.
     */
    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_RESET,
            0x71
        )
    );

    ESP_ERROR_CHECK(
        es7210_write_reg(
            ES7210_REG_RESET,
            0x41
        )
    );


    /*
     * Give the codec a moment to settle.
     */
    vTaskDelay(
        pdMS_TO_TICKS(20)
    );


    ESP_LOGI(
        TAG,
        "ES7210 initialized successfully"
    );

    return ESP_OK;
}


/* ============================================================
 * Audio initialization
 * ============================================================ */

esp_err_t sparky_audio_init(void)
{
    esp_err_t ret;


    /*
     * Use the shared I2C bus.
     */
    ret = sparky_i2c_init();

    if (ret != ESP_OK) {
        return ret;
    }


    i2c_master_bus_handle_t bus =
        sparky_i2c_get_bus();

    if (bus == NULL) {
        ESP_LOGE(
            TAG,
            "Shared I2C bus is NULL"
        );

        return ESP_ERR_INVALID_STATE;
    }


    /*
     * Add ES7210 to the existing I2C bus.
     */
    i2c_device_config_t es7210_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SPARKY_ES7210_I2C_ADDR,
        .scl_speed_hz = 400000,
    };


    ret = i2c_master_bus_add_device(
        bus,
        &es7210_config,
        &s_es7210
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "ES7210 I2C device creation failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    /*
     * Configure codec.
     */
    ret = es7210_init();

    if (ret != ESP_OK) {
        return ret;
    }


    /*
     * --------------------------------------------------------
     * I2S full duplex
     * --------------------------------------------------------
     */

    ESP_LOGI(
        TAG,
        "Initializing I2S RX/TX"
    );


    i2s_chan_config_t chan_config =
        I2S_CHANNEL_DEFAULT_CONFIG(
            SPARKY_AUDIO_I2S_PORT,
            I2S_ROLE_MASTER
        );

    chan_config.auto_clear = true;


    ret = i2s_new_channel(
        &chan_config,
        &s_tx_chan,
        &s_rx_chan
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S channel creation failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    /*
     * Standard I2S configuration.
     *
     * Official Waveshare example:
     *
     *   16 kHz
     *   16 bit
     *   stereo
     *   standard I2S
     *   MCLK = sample rate * 256
     */
    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(
            SPARKY_AUDIO_SAMPLE_RATE
        ),

        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_STEREO
            ),

        .gpio_cfg = {
            .mclk = SPARKY_AUDIO_MCLK_GPIO,
            .bclk = SPARKY_AUDIO_BCLK_GPIO,
            .ws   = SPARKY_AUDIO_WS_GPIO,
            .dout = SPARKY_AUDIO_DOUT_GPIO,
            .din  = SPARKY_AUDIO_DIN_GPIO,

            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };


    /*
     * Be explicit about the ES7210-required MCLK ratio.
     */
    std_config.clk_cfg.mclk_multiple =
        I2S_MCLK_MULTIPLE_256;


    ret = i2s_channel_init_std_mode(s_tx_chan, &std_config);

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S TX configuration failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }

    ret = i2s_channel_init_std_mode(s_rx_chan, &std_config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S RX configuration failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S TX enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_rx_chan);

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S enable failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    ESP_LOGI(
        TAG,
        "Audio initialized successfully"
    );

    ESP_LOGI(
        TAG,
        "Audio: 16kHz / 16-bit / stereo / I2S"
    );

    return ESP_OK;
}


/* ============================================================
 * ES8311 / speaker output
 * ============================================================ */

esp_err_t sparky_audio_output_init(void)
{
    if (s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_es8311 != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = sparky_i2c_get_bus();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * esp_codec_dev stores an 8-bit I2C write address and right-shifts
     * it for the IDF master driver. SPARKY_ES8311_I2C_ADDR is 7-bit 0x18.
     */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = SPARKY_ES8311_I2C_ADDR << 1,
        .bus_handle = bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctrl_if == NULL) {
        ESP_LOGE(TAG, "Failed to create ES8311 I2C control interface");
        return ESP_FAIL;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = SPARKY_AUDIO_I2S_PORT,
        .rx_handle = s_rx_chan,
        .tx_handle = s_tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (data_if == NULL || gpio_if == NULL) {
        ESP_LOGE(TAG, "Failed to create ES8311 data interfaces");
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk = true,
        .pa_pin = SPARKY_AUDIO_PA_CTRL_GPIO,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = 256,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    if (es8311_if == NULL) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec interface");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t device_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if = data_if,
    };
    s_es8311 = esp_codec_dev_new(&device_cfg);
    if (s_es8311 == NULL) {
        ESP_LOGE(TAG, "Failed to create ES8311 device");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = SPARKY_AUDIO_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_es8311, &sample_cfg) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open ES8311 at 16 kHz stereo");
        s_es8311 = NULL;
        return ESP_FAIL;
    }

    if (esp_codec_dev_set_out_vol(s_es8311, 75) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to set ES8311 volume");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ES8311 speaker output ready; PA on GPIO %d",
             SPARKY_AUDIO_PA_CTRL_GPIO);

    /* Keep the NS4150B off until real playback so the speaker stays quiet. */
    gpio_set_level(SPARKY_AUDIO_PA_CTRL_GPIO, 0);
    return ESP_OK;
}

esp_err_t sparky_audio_pa_enable(bool enable)
{
    return gpio_set_level(SPARKY_AUDIO_PA_CTRL_GPIO, enable ? 1 : 0);
}

esp_err_t sparky_audio_play_pcm(const int16_t *samples, size_t frame_count)
{
    if (s_es8311 == NULL || s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (samples == NULL || frame_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *data = (const char *)samples;
    size_t bytes_remaining = frame_count * 2 * sizeof(*samples);
    while (bytes_remaining > 0) {
        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_chan, data, bytes_remaining,
                                          &bytes_written, 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S TX write failed: %s", esp_err_to_name(ret));
            return ret;
        }
        if (bytes_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        data += bytes_written;
        bytes_remaining -= bytes_written;
    }
    return ESP_OK;
}

esp_err_t sparky_audio_output_test_tone(void)
{
    enum { TONE_HZ = 440, TONE_FRAMES = 256, TONE_DURATION_MS = 1500 };
    int16_t pcm[TONE_FRAMES * 2];
    uint32_t phase = 0;
    const uint32_t phase_step = (uint32_t)(((uint64_t)TONE_HZ << 32) /
                                            SPARKY_AUDIO_SAMPLE_RATE);
    const int chunk_count = (SPARKY_AUDIO_SAMPLE_RATE * TONE_DURATION_MS +
                             1000 * TONE_FRAMES - 1) /
                            (1000 * TONE_FRAMES);

    ESP_LOGI(TAG, "Playing %d Hz speaker test tone", TONE_HZ);
    for (int chunk = 0; chunk < chunk_count; ++chunk) {
        for (int frame = 0; frame < TONE_FRAMES; ++frame) {
            float angle = (float)phase * (6.283185307f / 4294967296.0f);
            int16_t sample = (int16_t)(sinf(angle) * 6000.0f);
            pcm[frame * 2] = sample;
            pcm[frame * 2 + 1] = sample;
            phase += phase_step;
        }

        esp_err_t ret = sparky_audio_play_pcm(pcm, TONE_FRAMES);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    ESP_LOGI(TAG, "Speaker test tone complete");
    return ESP_OK;
}


/* ============================================================
 * Audio test
 * ============================================================ */

esp_err_t sparky_audio_test(void)
{
    if (s_rx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }


    int16_t samples[512];

    size_t bytes_read = 0;


    esp_err_t ret = i2s_channel_read(
        s_rx_chan,
        samples,
        sizeof(samples),
        &bytes_read,
        1000
    );


    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S read failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    if (bytes_read == 0) {
        return ESP_OK;
    }


    size_t sample_count =
        bytes_read / sizeof(int16_t);


    int16_t min_value = 32767;
    int16_t max_value = -32768;

    double sum_squared = 0.0;


    for (size_t i = 0; i < sample_count; i++) {

        int16_t sample = samples[i];


        if (sample < min_value) {
            min_value = sample;
        }

        if (sample > max_value) {
            max_value = sample;
        }


        sum_squared +=
            (double)sample *
            (double)sample;
    }


    double rms =
        sqrt(
            sum_squared /
            sample_count
        );


    ESP_LOGI(
        TAG,
        "AUDIO: min=%d max=%d rms=%.1f",
        min_value,
        max_value,
        rms
    );


    return ESP_OK;
}


/* ============================================================
 * Audio read
 * ============================================================ */

esp_err_t sparky_audio_read(
    int16_t *samples,
    size_t sample_count
)
{
    if (s_rx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (samples == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_read = 0;

    esp_err_t ret = i2s_channel_read(
        s_rx_chan,
        samples,
        sample_count * sizeof(int16_t),
        &bytes_read,
        1000
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S read failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }

    if (bytes_read != sample_count * sizeof(int16_t)) {
        ESP_LOGW(
            TAG,
            "I2S short read: %u / %u bytes",
            (unsigned)bytes_read,
            (unsigned)(sample_count * sizeof(int16_t))
        );

        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
