/**
 ******************************************************************************
 * @file        touch.c
 * @version     V1.0
 * @brief       触摸屏 驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#include "touch.h"

/* 触摸屏控制器初始化参数 */
_m_tp_dev tp_dev =
{
    .init = tp_init,
    .scan = 0,
    .x = {0},
    .y = {0},
    .sta = 0,
    .touchtype = 0x00
};

/**
 * @brief       触摸屏初始化
 * @note        根据LCD的ID选择对应的触摸驱动:
 *              4.3寸(ST7102 TDDI, id=0x7102)使用ST7102触摸驱动(I2C地址0x55),
 *
 * @param       无
 * @retval      0,触摸屏初始化成功
 *              1,触摸屏有问题
 */
esp_err_t tp_init(void)
{
    tp_dev.touchtype = 0;                   /* 默认设置竖屏 */
    tp_dev.touchtype |= lcddev.dir & 0X01;  /* 根据LCD判定是横屏还是竖屏 */

    if (lcddev.id == 0x7102)                /* 4.3寸屏使用ST7102 TDDI集成触摸 */
    {
        if (st7102_init() != ESP_OK)
        {
            ESP_LOGE("TOUCH", "st7102_init failed");
        }
        tp_dev.scan = st7102_scan;          /* 扫描函数指向ST7102触摸屏扫描 */
    }
    else                                    /* 其余屏幕使用独立的GT9xxx电容触摸IC */
    {
        gt9xxx_init();                      /* 初始化gt9xxx器件 */
        tp_dev.scan = gt9xxx_scan;          /* 扫描函数指向GT触摸屏扫描 */
    }

    tp_dev.touchtype |= 0X80;               /* 电容屏 */
    return ESP_OK;
}

