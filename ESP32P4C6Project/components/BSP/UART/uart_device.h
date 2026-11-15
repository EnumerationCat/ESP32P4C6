/**
 * @file uart_device.h
 * @brief 串口BSP: 通用UART参数配置与驱动安装(8N1, 无流控)
 */

#ifndef __UART_DEVICE_H
#define __UART_DEVICE_H

#include "driver/uart.h"
#include "esp_err.h"

/* 驱动接收环缓冲大小(字节) */
#define UART_DEVICE_RX_BUF_SIZE    2048

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       配置UART参数并安装驱动
 * @note        UART0默认引脚由ROM/驱动处理, 无需额外uart_set_pin
 * @param       port: UART端口号
 * @param       baud_rate: 波特率
 * @retval      ESP_OK:成功
 */
esp_err_t uart_device_init(uart_port_t port, int baud_rate);

/**
 * @brief       卸载UART驱动
 * @param       port: UART端口号
 * @retval      ESP_OK:成功
 */
esp_err_t uart_device_deinit(uart_port_t port);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*__UART_DEVICE_H*/
