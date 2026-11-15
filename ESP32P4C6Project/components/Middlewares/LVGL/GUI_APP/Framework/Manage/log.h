/**
 * @file log.h
 * @brief 简易调试日志宏(Framework 公共工具, 与 container_of.h 同层)
 *
 * 输出: 文件:函数:行号: 内容, 直接走 stdout(UART0 控制台)。
 * 注意: 不做日志级别过滤; 需要级别/时间戳请用 ESP_LOGI/ESP_LOGE 等。
 */

#ifndef LOG_H
#define LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 防止与其他库的同名宏冲突 */
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...)  printf("%s:%s:%d: " fmt, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LOG_H*/
