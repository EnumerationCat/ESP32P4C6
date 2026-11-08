/**
 ******************************************************************************
 * @file        st7102.c
 * @version     V1.0 (Hardware I2C version)
 * @brief       ST7102(TDDI)触摸驱动 
 * @note        读时序遵循协议: 写地址后STOP再重新START读.
 *              注意: 触摸扫描窗口由显示消隐期提供, 必须配合正确的DSI视频时序
 *              (VFP=200, 约40Hz帧率)使用, 否则扫描电路不启动.
 ******************************************************************************
 */

#include "st7102.h"

const char *st7102_tag = "st7102";
i2c_master_dev_handle_t st7102_handle = NULL;   /* ST7102设备句柄(MYIIC硬件I2C总线) */
uint8_t g_st7102_tnum = 2;

/* 向ST7102写入数据 */
esp_err_t st7102_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    esp_err_t ret;
    uint8_t *wr_buf = malloc(2 + len);

    if (wr_buf == NULL)
    {
        ESP_LOGE(st7102_tag, "%s memory failed", __func__);
        return ESP_ERR_NO_MEM;      /* 分配内存失败 */
    }

    wr_buf[0] = reg >> 8;           /* 寄存器地址高8位 */
    wr_buf[1] = reg & 0XFF;         /* 寄存器地址低8位 */

    memcpy(wr_buf + 2, buf, len);   /* 拷贝数据至发送缓冲区 */

    ret = i2c_master_transmit(st7102_handle, wr_buf, 2 + len, -1);

    free(wr_buf);                   /* 发送完成释放内存 */

    return ret;
}

/**
 * @brief       从ST7102读出数据
 * @note        ST7102读时序: 写完2字节寄存器地址后必须发完整STOP, 再重新START
 *              读数据, 因此用两次独立事务, 不能用i2c_master_transmit_receive
 *              (其写/读之间是repeated-START, 无STOP)
 */
esp_err_t st7102_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    esp_err_t ret;
    uint8_t memaddr_buf[2];
    memaddr_buf[0] = reg >> 8;      /* 寄存器地址高8位 */
    memaddr_buf[1] = reg & 0XFF;    /* 寄存器地址低8位 */

    ret = i2c_master_transmit(st7102_handle, memaddr_buf, sizeof(memaddr_buf), -1);

    if (ret != ESP_OK)
    {
        return ret;
    }

    return i2c_master_receive(st7102_handle, buf, len, -1);
}

/**
 * @brief       初始化ST7102触摸屏
 * @param       无
 * @retval      0, 初始化成功; 1, 初始化失败;
 */
esp_err_t st7102_init(void)
{
    uint8_t temp[5];
    uint8_t i;

    /* 初始化MYIIC硬件I2C */
    if (bus_handle == NULL)
    {
        ESP_ERROR_CHECK(myiic_init());
    }

    i2c_device_config_t st7102_i2c_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,      /* 从机地址长度 */
        .scl_speed_hz    = IIC_SPEED_CLK,           /* 传输速率 */
        .device_address  = ST7102_DEV_ID,           /* 从机7位地址(0x55) */
    };
    /* I2C总线上添加ST7102设备 */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &st7102_i2c_dev_conf, &st7102_handle));

    /* 配置RST引脚为输出 */
    gpio_config_t gpio_init_struct = {0};
    gpio_init_struct.intr_type    = GPIO_INTR_DISABLE;
    gpio_init_struct.mode         = GPIO_MODE_OUTPUT;
    gpio_init_struct.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pin_bit_mask = 1ull << ST7102_RST_GPIO_PIN;
    gpio_config(&gpio_init_struct);

    /* 配置INT引脚为输入 */
    gpio_init_struct.mode         = GPIO_MODE_INPUT;
    gpio_init_struct.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pin_bit_mask = 1ull << ST7102_INT_GPIO_PIN;
    gpio_config(&gpio_init_struct);

    /* 硬件复位 */
    ST7102_RST(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    ST7102_RST(1);
    vTaskDelay(pdMS_TO_TICKS(100)); 

    /* 等待芯片进入Normal状态 */
    for (i = 0; i < 50; i++)
    {
        st7102_rd_reg(ST7102_STATUS_REG, &temp[0], 1);

        if ((temp[0] & 0X0F) == 0X00)
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if ((temp[0] & 0X0F) != 0X00)
    {
        ESP_LOGE(st7102_tag, "ST7102 not ready, status=0x%02X", temp[0]);
        return ESP_FAIL;
    }

    /* 读取最大触摸点数 */
    st7102_rd_reg(ST7102_MAX_TOUCH_REG, &temp[0], 1);

    if (temp[0] == 0 || temp[0] > CT_MAX_TOUCH)
    {
        ESP_LOGE(st7102_tag, "Invalid max_touch: %d", temp[0]);
        return ESP_FAIL;
    }

    g_st7102_tnum = temp[0];

    /* 读取固件版本 */
    st7102_rd_reg(ST7102_FW_VERSION_REG, &temp[0], 1);
    st7102_rd_reg(ST7102_FW_REVISION_REG, &temp[1], 4);

    ESP_LOGI(st7102_tag, "CTP:ST7102 Ver:%d Rev:%d.%d.%d.%d tnum:%d",
             temp[0], temp[1], temp[2], temp[3], temp[4], g_st7102_tnum);

    /* 触摸MCU需在触摸参数写入后重新复位, 才会带着参数重启扫描电路 */
    temp[0] = 0x01;                                 
    st7102_wr_reg(ST7102_DEVICE_CTRL_REG, temp, 1);
    vTaskDelay(pdMS_TO_TICKS(60));                  

    for (i = 0; i < 50; i++)                         
    {
        st7102_rd_reg(ST7102_STATUS_REG, &temp[0], 1);

        if ((temp[0] & 0X0F) == 0X00)
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    temp[0] = 0x00;
    st7102_wr_reg(ST7102_DEVICE_CTRL_REG, temp, 1);  /* 确保工作在正常模式 */

    uint8_t cnt_buf[2];
    st7102_rd_reg(0x000A, cnt_buf, 2);
    uint16_t cnt1 = ((uint16_t)cnt_buf[0] << 8) | cnt_buf[1];
    vTaskDelay(pdMS_TO_TICKS(100));
    st7102_rd_reg(0x000A, cnt_buf, 2);
    uint16_t cnt2 = ((uint16_t)cnt_buf[0] << 8) | cnt_buf[1];
    ESP_LOGI(st7102_tag, "Sensing counter: %u -> %u (%s)", cnt1, cnt2,
             cnt2 > cnt1 ? "扫描电路已启动" : "扫描电路仍未启动!");

    return ESP_OK;
}

/**
 * @brief       扫描触摸屏(采用轮询方式)
 * @param       mode : 电容屏未用到该参数, 为了兼容电阻屏
 * @retval      当前触屏状态
 *   @arg       0, 触屏无触摸; 
 *   @arg       1, 触屏有触摸;
 */
uint8_t st7102_scan(uint8_t mode)
{
    uint8_t buf[CT_MAX_TOUCH * ST7102_POINT_DATA_LEN];
    uint8_t i = 0;
    uint8_t res = 0;
    uint8_t point_num = 0;
    uint8_t need_read = 0;
    uint16_t tempsta = tp_dev.sta;

    /* 检查 INT 引脚状态(低电平有效, 表示有触摸事件) */
    if (ST7102_INT == ST7102_INT_ACTIVE)
    {
        uint8_t touch_info = 0;
        st7102_rd_reg(ST7102_ADV_TOUCH_INFO_REG, &touch_info, 1);

        if (touch_info & ST7102_WITH_COORD)
        {
            need_read = 1;
        }
    }

    if (tempsta & TP_PRES_DOWN)
    {
        need_read = 1;
    }

    if (!need_read)
    {
        return 0;
    }

    /* 读取坐标数据 */
    st7102_rd_reg(ST7102_TP1_REG, buf, g_st7102_tnum * ST7102_POINT_DATA_LEN);

    tp_dev.sta &= 0xE000;

    for (i = 0; i < g_st7102_tnum; i++)
    {
        if (buf[i * ST7102_POINT_DATA_LEN] & ST7102_VALID_MASK)
        {
            tp_dev.sta |= (1 << i);
            point_num++;

            /* ST7102是TDDI, 触摸坐标跟随显示扫描方向, 与显示坐标直接对应 */
            tp_dev.x[i] = ((uint16_t)(buf[i * ST7102_POINT_DATA_LEN] & 0X3F) << 8) + buf[i * ST7102_POINT_DATA_LEN + 1];
            tp_dev.y[i] = ((uint16_t)(buf[i * ST7102_POINT_DATA_LEN + 2] & 0X3F) << 8) + buf[i * ST7102_POINT_DATA_LEN + 3];
        }
    }

    if (point_num)
    {
        tp_dev.sta |= TP_PRES_DOWN | TP_CATH_PRES;
        res = 1;
    }
    else
    {
        if (tempsta & TP_PRES_DOWN)
        {
            tp_dev.sta &= ~TP_PRES_DOWN;
        }
        else
        {
            tp_dev.x[0] = 0xffff;
            tp_dev.y[0] = 0xffff;
            tp_dev.sta &= 0XE000;
        }
    }

    return res;
}
