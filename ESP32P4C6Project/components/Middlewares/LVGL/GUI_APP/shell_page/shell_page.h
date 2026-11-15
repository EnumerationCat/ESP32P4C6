/**
 * @file shell_page.h
 * @brief 命令行页面(shell_page): 对外接口 + LCD文本页显示
 *
 * 页面 = 显示层(view) + 模块初始化接口:
 *  - init 创建数据层、安装UART0驱动并启动shell任务;
 *  - tick/open 由LVGL任务调用, 维护LCD文本页;
 *  - puts/printf/clear/input_push 供命令实现和输入源使用的model快捷接口。
 *
 * 依赖: Framework/shell_core(数据层)、Framework/Model/shell_input_uart(输入源)。
 */

#ifndef SHELL_PAGE_H
#define SHELL_PAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief       初始化shell页面: 创建数据层、安装UART0驱动并启动shell任务
 * @note        不调用LVGL接口, 可在lv_init()之前调用
 * @param       无
 * @retval      无
 */
void shell_page_init(void);

/**
 * @brief       周期刷新LCD文本页(只在LVGL任务中调用)
 * @note        与 lv_timer_handler() 同一任务中周期调用
 * @param       无
 * @retval      无
 */
void shell_page_tick(void);

/**
 * @brief       切换到LCD文本页(只在LVGL任务中调用)
 * @note        进入时把终端文本重置为一行提示符, 并请求串口补打提示符
 * @param       无
 * @retval      无
 */
void shell_page_open(void);

/**
 * @brief       向UART和终端文本缓冲输出字符串(供命令实现使用)
 * @param       s: 以'\0'结尾的字符串
 * @retval      无
 */
void shell_page_puts(const char *s);

/**
 * @brief       格式化输出到UART和终端文本缓冲(供命令实现使用)
 * @param       fmt: 格式字符串
 * @retval      无
 */
void shell_page_printf(const char *fmt, ...);

/**
 * @brief       清空UART终端和终端文本缓冲(供命令实现使用)
 * @param       无
 * @retval      无
 */
void shell_page_clear(void);

/**
 * @brief       向shell投递一个输入字节(键盘/串口等输入源统一入口)
 * @note        可在任意任务/回调中调用; 非阻塞, 队列满时丢弃该字节
 * @param       ch: 输入字节(与终端键盘一致: 可打印ASCII或ESC转义序列)
 * @retval      无
 */
void shell_page_input_push(uint8_t ch);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*SHELL_PAGE_H*/
