/**
 * @file shell_input_uart.c
 * @brief 串口输入源: 安装UART0驱动并把收到的字节投递给shell_core
 */

#include "shell_input_uart.h"
#include "shell_core.h"

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
    const uart_config_t cfg =
    {
        .baud_rate = SHELL_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(SHELL_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_driver_install(SHELL_UART_NUM, 2048, 0, 0, NULL, 0));

    xTaskCreate(shell_uart_rx_task, "shell_uart_rx", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "UART0 ready, 115200");
}
