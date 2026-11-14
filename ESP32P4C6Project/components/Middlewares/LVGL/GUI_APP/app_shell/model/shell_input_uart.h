/**
 * @file shell_input_uart.h
 * @brief 串口输入源: 安装UART0驱动并把收到的字节投递给shell_core
 */

#ifndef SHELL_INPUT_UART_H
#define SHELL_INPUT_UART_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       安装UART0驱动并启动采集任务
 * @note        在 shell_core_init() 之后调用; 采集到的字节通过
 *              shell_core_input_push_wait() 投递给shell任务
 * @param       无
 * @retval      无
 */
void shell_input_uart_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*SHELL_INPUT_UART_H*/
