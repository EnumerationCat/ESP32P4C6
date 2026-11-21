/**
 * @file usb_msc_device.h
 * @brief USB MSC 设备模式: 把SD卡作为U盘暴露给PC
 *
 * 板上的USB-A口(高速OTG)同一时刻只能工作在Host或Device:
 *   - 正常模式: Host, 接USB鼠标(HID)
 *   - U盘模式: Device, PC可以读写SD卡
 *
 * usb_msc_device_start() 会卸载USB Host、卸载 /User 的FATFS并重新按裸卡初始化,
 * 然后启动TinyUSB MSC; usb_msc_device_stop() 反向恢复。
 * 模式切换期间不能有视频播放/摄像头预览正在占用SD卡。
 */

#ifndef __USB_MSC_DEVICE_H
#define __USB_MSC_DEVICE_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       进入U盘模式(PC可读写SD卡)
 * @note        会卸掉USB鼠标和 /User 挂载; 调用前应确保没有视频在播放
 * @param       无
 * @retval      ESP_OK:成功; 其他:失败(已回滚到正常模式)
 */
esp_err_t usb_msc_device_start(void);

/**
 * @brief       退出U盘模式: 停止TinyUSB, 重新挂载SD卡和USB鼠标
 * @param       无
 * @retval      ESP_OK:成功; 其他:SD卡重新挂载失败等
 */
esp_err_t usb_msc_device_stop(void);

/**
 * @brief       U盘模式是否已开启
 * @param       无
 * @retval      true:PC可作为U盘访问SD卡
 */
bool usb_msc_device_active(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __USB_MSC_DEVICE_H */
