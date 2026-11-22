/**
 * @file i2s_audio.c
 * @brief I2S0 音频输出驱动(ES8311)
 *
 * 与原例程的差异: 用 esp_err_t 返回值代替 ESP_ERROR_CHECK,
 * 音频初始化失败时只影响声音, 不能让整个播放器复位。
 */

#include "i2s_audio.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "i2s_audio";

i2s_chan_handle_t tx_handle = NULL;     /* I2S发送通道句柄 */
i2s_chan_handle_t rx_handle = NULL;     /* I2S接收通道句柄 */
static i2s_std_config_t s_std_cfg;      /* 标准模式配置(引脚/位宽), 换采样率时复用 */

#define I2S_AUDIO_DMA_DESC_NUM      8       /* DMA描述符个数 */
#define I2S_AUDIO_DMA_TARGET_MS     60      /* DMA缓冲目标时长(ms) */
#define I2S_AUDIO_BYTES_PER_FRAME   4       /* 16bit立体声: 每帧4字节 */

/**
 * @brief       计算每个DMA描述符需要的帧数
 * @param       samplerate       : 音频采样率
 * @param       audio_chunk_bytes: AVI中一个音频块的大小(字节)
 * @retval      每个描述符的帧数
 */
static uint32_t i2s_audio_calc_dma_frame_num(uint32_t samplerate, uint32_t audio_chunk_bytes)
{
    uint64_t frames_by_time;
    uint64_t frames_by_chunk;
    uint64_t total_frames;
    uint32_t per_desc;

    if (samplerate == 0)
    {
        samplerate = I2S_SAMPLE_RATE;
    }

    if ((audio_chunk_bytes == 0) || (audio_chunk_bytes > 8192))
    {
        audio_chunk_bytes = 4096;       /* 异常值按4096字节处理 */
    }

    frames_by_time  = (uint64_t)samplerate * I2S_AUDIO_DMA_TARGET_MS / 1000;
    frames_by_chunk = (uint64_t)(audio_chunk_bytes / I2S_AUDIO_BYTES_PER_FRAME) * 12 / 10;
    total_frames    = (frames_by_time > frames_by_chunk) ? frames_by_time : frames_by_chunk;
    per_desc        = (uint32_t)((total_frames + I2S_AUDIO_DMA_DESC_NUM - 1) / I2S_AUDIO_DMA_DESC_NUM);

    if (per_desc < 64)
    {
        per_desc = 64;                  /* 下限 */
    }

    if (per_desc > 1008)
    {
        per_desc = 1008;                /* 单个描述符最大4032字节(16bit立体声), 超过会被驱动截断 */
    }

    return per_desc;
}

/**
 * @brief       初始化I2S(开机调用一次)
 * @note        默认16kHz/512帧每描述符, 只用于保证通道存在;
 *              视频播放前会调用 i2s_audio_setup_for_samplerate() 重建通道
 * @param       无
 * @retval      ESP_OK:成功; 其他:失败
 */
esp_err_t i2s_audio_init(void)
{
    esp_err_t ret;

    if (tx_handle != NULL)
    {
        return ESP_OK;                  /* 已初始化 */
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = I2S_AUDIO_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = 512;
    chan_cfg.auto_clear    = true;      /* 自动清除DMA缓冲区遗留的数据 */

    ret = i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        tx_handle = NULL;
        rx_handle = NULL;
        return ret;
    }

    s_std_cfg = (i2s_std_config_t) {
        .clk_cfg = {
            .sample_rate_hz = I2S_SAMPLE_RATE,
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple  = I2S_MCLK_MULTIPLE,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode      = I2S_SLOT_MODE_STEREO,
            .slot_mask      = I2S_STD_SLOT_BOTH,
            .ws_width       = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol         = false,
            .bit_shift      = true,
            .left_align     = true,
            .big_endian     = false,
            .bit_order_lsb  = false,
        },
        .gpio_cfg = {
            .mclk = I2S_MCK_IO,
            .bclk = I2S_BCK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din  = I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(tx_handle, &s_std_cfg);
    if (ret == ESP_OK)
    {
        ret = i2s_channel_init_std_mode(rx_handle, &s_std_cfg);
    }

    if (ret == ESP_OK)
    {
        ret = i2s_channel_enable(tx_handle);
    }

    if (ret == ESP_OK)
    {
        ret = i2s_channel_enable(rx_handle);
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s std init failed: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_handle);
        i2s_del_channel(rx_handle);
        tx_handle = NULL;
        rx_handle = NULL;
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief       按采样率和音频块大小重建I2S通道
 * @note        帧数只能在 i2s_new_channel() 时指定, 采样率变化时要重建通道。
 *              调用后通道处于禁用状态, 需要再调 i2s_trx_start()。
 * @param       samplerate       : 音频采样率
 * @param       audio_chunk_bytes: AVI中一个音频块的大小(字节)
 * @retval      ESP_OK:成功; 其他:失败
 */
esp_err_t i2s_audio_setup_for_samplerate(uint32_t samplerate, uint32_t audio_chunk_bytes)
{
    esp_err_t ret;
    uint32_t per_desc;

    if (tx_handle == NULL || rx_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;   /* 必须先调用 i2s_audio_init() */
    }

    if (samplerate == 0)
    {
        samplerate = I2S_SAMPLE_RATE;
    }

    per_desc = i2s_audio_calc_dma_frame_num(samplerate, audio_chunk_bytes);

    /* 已有通道: 先禁用再删除(未使能时禁用会报错, 这里忽略) */
    i2s_channel_disable(tx_handle);
    i2s_channel_disable(rx_handle);

    ret = i2s_del_channel(tx_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "delete tx channel failed: %s", esp_err_to_name(ret));
        return ret;
    }
    tx_handle = NULL;

    ret = i2s_del_channel(rx_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "delete rx channel failed: %s", esp_err_to_name(ret));
        return ret;
    }
    rx_handle = NULL;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear    = true;
    chan_cfg.dma_desc_num  = I2S_AUDIO_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = per_desc;

    ret = i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "recreate i2s channel failed: %s", esp_err_to_name(ret));
        tx_handle = NULL;
        rx_handle = NULL;
        return ret;
    }

    i2s_std_config_t std_cfg = s_std_cfg;           /* 复用引脚/位宽配置, 只改采样率 */
    std_cfg.clk_cfg.sample_rate_hz = samplerate;

    ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (ret == ESP_OK)
    {
        ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s std init failed: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_handle);
        i2s_del_channel(rx_handle);
        tx_handle = NULL;
        rx_handle = NULL;
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief       启动I2S收发
 * @param       无
 * @retval      ESP_OK:成功; 其他:失败
 */
esp_err_t i2s_trx_start(void)
{
    esp_err_t ret;

    if (tx_handle == NULL || rx_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return i2s_channel_enable(rx_handle);
}

/**
 * @brief       停止I2S收发(已停止时忽略错误)
 * @param       无
 * @retval      无
 */
void i2s_trx_stop(void)
{
    if (tx_handle != NULL)
    {
        i2s_channel_disable(tx_handle);
    }

    if (rx_handle != NULL)
    {
        i2s_channel_disable(rx_handle);
    }
}

/**
 * @brief       阻塞发送一段音频数据
 * @param       buffer    : 数据首地址
 * @param       frame_size: 数据字节数
 * @retval      实际发送的字节数(失败返回0)
 */
size_t i2s_tx_write(uint8_t *buffer, uint32_t frame_size)
{
    size_t bytes_written = 0;
    esp_err_t ret;

    if (tx_handle == NULL || buffer == NULL || frame_size == 0)
    {
        return 0;
    }

    ret = i2s_channel_write(tx_handle, buffer, frame_size, &bytes_written, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "i2s write failed: %s", esp_err_to_name(ret));
        return 0;
    }

    return bytes_written;
}
