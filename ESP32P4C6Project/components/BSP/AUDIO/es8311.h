/**
 * @file es8311.h
 * @brief ES8311 音频编解码器驱动
 *
 * 与原例程的差异: I2C 总线不再自己创建, 而是复用摄像头 SCCB 总线
 * (GPIO28/29), 避免同一组引脚被两个 I2C 主机重复初始化。
 */

#ifndef __ES8311_H_
#define __ES8311_H_

#include <stdbool.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

/* 功放使能引脚 */
#define PA_CTRL_GPIO_PIN        GPIO_NUM_11

/**
 * @brief       初始化ES8311(需先调用i2s_audio_init)
 * @param       bus_handle: 已创建好的I2C主机总线(摄像头SCCB总线)
 * @retval      ESP_OK:成功; 其他:失败
 */
esp_err_t es8311_init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief       编解码器是否可用
 * @param       无
 * @retval      true:初始化成功, 可以播放音频
 */
bool es8311_ready(void);

#endif
