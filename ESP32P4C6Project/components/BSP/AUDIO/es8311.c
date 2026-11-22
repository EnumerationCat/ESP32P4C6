/**
 * @file es8311.c
 * @brief ES8311 音频编解码器驱动
 *
 * 控制口走摄像头共享的 I2C 总线(ES8311 地址 0x30, 7bit 0x18),
 * 数据口直连 i2s_audio 的 I2S0 收发通道; 本板 ES8311 工作在从机模式,
 * 时钟由 ESP32-P4 的 I2S 主机提供(MCLK = 384 * fs)。
 */

#include "es8311.h"
#include "i2s_audio.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

static const char *TAG = "es8311";

static esp_codec_dev_handle_t s_codec = NULL;   /* 顶层编解码器设备句柄 */
static bool s_ready = false;                    /* 初始化完成 */

/**
 * @brief       初始化ES8311(需先调用i2s_audio_init)
 * @param       bus_handle: 已创建好的I2C主机总线(摄像头SCCB总线)
 * @retval      ESP_OK:成功; 其他:失败
 */
esp_err_t es8311_init(i2c_master_bus_handle_t bus_handle)
{
    if (s_ready)
    {
        return ESP_OK;
    }

    if (bus_handle == NULL)
    {
        ESP_LOGE(TAG, "no i2c bus handle");
        return ESP_ERR_INVALID_ARG;
    }

    if (tx_handle == NULL || rx_handle == NULL)
    {
        ESP_LOGE(TAG, "i2s is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* 控制接口: 复用已有 I2C 总线, 只添加 ES8311 设备 */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bus_handle,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctrl_if == NULL)
    {
        ESP_LOGE(TAG, "create i2c ctrl interface failed");
        return ESP_FAIL;
    }

    /* 数据接口: 直接使用 i2s_audio 创建的收发通道 */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (data_if == NULL)
    {
        ESP_LOGE(TAG, "create i2s data interface failed");
        return ESP_FAIL;
    }

    /* ES8311 设备 */
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (gpio_if == NULL)
    {
        ESP_LOGE(TAG, "create gpio interface failed");
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if      = ctrl_if,
        .gpio_if      = gpio_if,
        .codec_mode   = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode  = false,                  /* I2S 主机是 ESP32, 编解码器做从机 */
        .use_mclk     = true,
        .pa_pin       = PA_CTRL_GPIO_PIN,
        .pa_reverted  = false,
        .hw_gain = {
            .pa_voltage        = 5.0f,
            .codec_dac_voltage = 3.3f,
        },
        .mclk_div     = I2S_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    if (es8311_if == NULL)
    {
        ESP_LOGE(TAG, "create es8311 interface failed");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if  = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (s_codec == NULL)
    {
        ESP_LOGE(TAG, "create codec device failed");
        return ESP_FAIL;
    }

    /* 打开设备: 16bit 立体声 16kHz */
    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = 16,
        .channel         = 2,
        .channel_mask    = 0x03,
        .sample_rate     = I2S_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_codec, &sample_cfg) != ESP_CODEC_DEV_OK)
    {
        ESP_LOGE(TAG, "open codec device failed");
        return ESP_FAIL;
    }

    if (esp_codec_dev_set_out_vol(s_codec, EXAMPLE_VOICE_VOLUME) != ESP_CODEC_DEV_OK)
    {
        ESP_LOGW(TAG, "set output volume failed");
    }

    s_ready = true;
    ESP_LOGI(TAG, "codec ready (16kHz, stereo, 16bit)");
    return ESP_OK;
}

/**
 * @brief       编解码器是否可用
 * @param       无
 * @retval      true:初始化成功, 可以播放音频
 */
bool es8311_ready(void)
{
    return s_ready;
}
