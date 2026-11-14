/**
 * @file app_shell.c
 * @brief 命令行门面(facade): 把原有对外接口转发到 model / view
 *
 * 初始化顺序:
 *  1. shell_core_init()     创建互斥锁和输入队列
 *  2. shell_input_uart_init() 安装UART0驱动并启动采集任务(可开始投递字节)
 *  3. shell_core_start()    启动shell任务(启动横幅需要UART已就绪)
 */

#include "app_shell.h"
#include "shell_core.h"
#include "shell_input_uart.h"
#include "shell_view.h"

#include <stdarg.h>

void app_shell_init(void)
{
    shell_core_init();
    shell_input_uart_init();
    shell_core_start();
}

void app_shell_tick(void)
{
    shell_view_tick();
}

void app_shell_open(void)
{
    shell_view_open();
}

void app_shell_puts(const char *s)
{
    shell_core_puts(s);
}

void app_shell_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    shell_core_vprintf(fmt, ap);
    va_end(ap);
}

void app_shell_clear(void)
{
    shell_core_clear();
}

void app_shell_input_push(uint8_t ch)
{
    shell_core_input_push(ch);
}
