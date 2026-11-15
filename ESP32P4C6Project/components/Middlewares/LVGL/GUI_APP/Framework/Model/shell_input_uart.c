/**
 * @file shell_input_uart.c
 * @brief 串口输入源: 通过BSP安装UART驱动并把收到的字节投递给shell_core
 */

#include "shell_input_uart.h"
#include "shell_core.h"
#include "uart_device.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "SHELL_UART";

/* 只负责把UART收到的字节投进队列, 按键解析在shell任务里统一做 */
static void shell_uart_rx_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        uint8_t ch;

        if (uart_read_bytes(SHELL_UART_NUM, &ch, 1, portMAX_DELAY) == 1)
        {
            /* 允许等待: shell忙时把背压传回UART环缓冲, 尽量不丢字节 */
            shell_core_input_push_wait(ch);
        }
    }
}

void shell_input_uart_init(void)
{
    ESP_ERROR_CHECK(uart_device_init(SHELL_UART_NUM, SHELL_UART_BAUD));

    xTaskCreate(shell_uart_rx_task, "shell_uart_rx", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "UART%d ready, %d baud", (int)SHELL_UART_NUM, SHELL_UART_BAUD);
}
