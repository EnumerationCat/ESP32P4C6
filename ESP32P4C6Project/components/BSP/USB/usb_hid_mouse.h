/**
 * @file usb_hid_mouse.h
 * @brief USB HID 鼠标驱动: 读取插在 USB-A 口上的鼠标位移和按键
 *
 * 用法:
 *   app_main 中调用一次 usb_hid_mouse_init() 安装 USB Host 和 HID 类驱动;
 *   其他任务(如 LVGL 输入设备)周期调用 usb_hid_mouse_read() 取增量位移,
 *   由调用方自己维护光标的绝对坐标。
 *
 * 报表回调运行在 USB 后台任务中, read/connected 可在其他任务中调用(内部加锁),
 * 读取时会清空累计位移, 因此只能有一个消费者。
 */

#ifndef __USB_HID_MOUSE_H
#define __USB_HID_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *      TYPEDEFS
 **********************/

/* 一次读取到的鼠标增量数据 */
typedef struct
{
    int32_t x;          /* 相对X位移, 向右为正 */
    int32_t y;          /* 相对Y位移, 向下为正 */
    uint8_t buttons;    /* HID boot协议按键: bit0左键, bit1右键, bit2中键 */
} usb_hid_mouse_report_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief       初始化USB HID鼠标: 安装USB Host和HID类驱动
 * @note        与USB Host相关的其他驱动(UVC等)不能重复安装USB Host
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t usb_hid_mouse_init(void);

/**
 * @brief       卸载USB HID鼠标: 注销HID类驱动并卸载USB Host
 * @note        用于切换USB口工作模式(如U盘模式需要该口做Device)。
 *              卸载后可用 usb_hid_mouse_init() 重新安装。
 * @param       无
 * @retval      ESP_OK:卸载完成
 */
esp_err_t usb_hid_mouse_deinit(void);

/**
 * @brief       读取自上次调用以来累计的鼠标位移和当前按键状态
 * @note        非阻塞, 可在任意任务中调用
 * @param       report: 输出报表(仅在返回true时有效)
 * @retval      true:读到新的HID报表; false:没有新数据
 */
bool usb_hid_mouse_read(usb_hid_mouse_report_t *report);

/**
 * @brief       鼠标是否已连接
 * @param       无
 * @retval      true:已连接
 */
bool usb_hid_mouse_connected(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*__USB_HID_MOUSE_H*/
