/**
 * @file app_shell.h
 * @brief 命令行门面(facade): 转发到 model(shell_core)和 view(shell_view)
 *
 * 分层结构:
 *  - model/shell_core.*      数据层: 输入队列、行编辑、历史、终端文本缓冲、命令执行
 *  - model/shell_input_uart.* 串口输入源
 *  - view/shell_view.*       显示层: LCD文本页、终端标签、输入光标
 *  - 本文件只保留原有对外接口, 方便 main/user_home/命令实现无需改动
 */

#ifndef APP_SHELL_H
#define APP_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief       初始化app_shell: 创建数据层、安装UART0驱动并启动shell任务
 * @note        不调用LVGL接口, 可在lv_init()之前调用
 * @param       无
 * @retval      无
 */
void app_shell_init(void);

/**
 * @brief       刷新LCD镜像文本(转发到shell_view_tick)
 * @note        必须在LVGL任务上下文中周期调用(与lv_timer_handler同一任务)
 * @param       无
 * @retval      无
 */
void app_shell_tick(void);

/**
 * @brief       切换到LCD文本页(转发到shell_view_open)
 * @note        只能在LVGL任务上下文中调用(例如按钮回调)
 * @param       无
 * @retval      无
 */
void app_shell_open(void);

/**
 * @brief       向UART和终端文本缓冲输出字符串(供命令实现使用)
 * @param       s: 以'\0'结尾的字符串
 * @retval      无
 */
void app_shell_puts(const char *s);

/**
 * @brief       格式化输出到UART和终端文本缓冲(供命令实现使用)
 * @param       fmt: 格式字符串
 * @retval      无
 */
void app_shell_printf(const char *fmt, ...);

/**
 * @brief       清空UART终端和终端文本缓冲(供命令实现使用)
 * @param       无
 * @retval      无
 */
void app_shell_clear(void);

/**
 * @brief       向shell投递一个输入字节(键盘/串口等输入源统一入口)
 * @note        可在任意任务/回调中调用; 非阻塞, 队列满时丢弃该字节
 * @param       ch: 输入字节(与终端键盘一致: 可打印ASCII或ESC转义序列)
 * @retval      无
 */
void app_shell_input_push(uint8_t ch);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*APP_SHELL_H*/
