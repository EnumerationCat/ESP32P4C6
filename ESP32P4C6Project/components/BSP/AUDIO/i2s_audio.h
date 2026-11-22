/**
 * @file i2s_audio.h
 * @brief I2S0 音频输出驱动(ES8311)
 *
 * I2S0 主机模式, 16bit 立体声, MCLK = 384 * 采样率;
 * 播放 AVI 前需要按文件采样率重建通道(每个视频一次), 因为
 * 每个 DMA 描述符的帧数只能在 i2s_new_channel() 时指定。
 */

#ifndef _I2S_AUDIO_H_
#define _I2S_AUDIO_H_

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"

/* ES8311 音频引脚(ESP32-P4 开发板) */
#define I2S_NUM                 (I2S_NUM_0)                /* I2S port */
#define I2S_BCK_IO              (GPIO_NUM_4)               /* ES8311_SCLK */
#define I2S_WS_IO               (GPIO_NUM_6)               /* ES8311_LRCK */
#define I2S_DO_IO               (GPIO_NUM_3)               /* ES8311_SDIN */
#define I2S_DI_IO               (GPIO_NUM_5)               /* ES8311_SDOUT */
#define I2S_MCK_IO              (GPIO_NUM_2)               /* ES8311_MCLK */
#define I2S_SAMPLE_RATE         (16000)                    /* 默认采样率 */
#define I2S_MCLK_MULTIPLE       (384)                      /* MCLK = 384 * 采样率 */
#define EXAMPLE_VOICE_VOLUME    50                         /* 输出音量(0~100) */

extern i2s_chan_handle_t tx_handle;                        /* I2S发送通道句柄 */
extern i2s_chan_handle_t rx_handle;                        /* I2S接收通道句柄 */

esp_err_t i2s_audio_init(void);                            /* I2S初始化(开机调用一次) */
esp_err_t i2s_audio_setup_for_samplerate(uint32_t samplerate, uint32_t audio_chunk_bytes); /* 按采样率重建通道 */
esp_err_t i2s_trx_start(void);                             /* 启动I2S收发 */
void i2s_trx_stop(void);                                   /* 停止I2S收发 */
size_t i2s_tx_write(uint8_t *buffer, uint32_t frame_size); /* 阻塞发送音频数据 */

#endif
