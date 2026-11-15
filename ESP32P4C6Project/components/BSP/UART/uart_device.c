#include "uart_device.h"

/**
 * @brief       配置UART参数并安装驱动
 * @param       port: UART端口号
 * @param       baud_rate: 波特率
 * @retval      ESP_OK:成功
 */
esp_err_t uart_device_init(uart_port_t port, int baud_rate)
{
    const uart_config_t cfg =
    {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(port, &cfg);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return uart_driver_install(port, UART_DEVICE_RX_BUF_SIZE, 0, 0, NULL, 0);
}

/**
 * @brief       卸载UART驱动
 * @param       port: UART端口号
 * @retval      ESP_OK:成功
 */
esp_err_t uart_device_deinit(uart_port_t port)
{
    return uart_driver_delete(port);
}
