/**
 ******************************************************************************
 * @file        st7102.h
 * @version     V1.0
 * @brief       电容触摸屏-ST7102 (TDDI) 驱动代码 (ESP32-P4移植)
 *   @note       ST7102是触控与显示集成(TDDI)芯片, 4.3寸屏(WKS43WV076-WCT)使用.
 *               触摸部分通过I2C(从机地址0x55)通信, 与GT9xxx不同, 需单独驱动.
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#ifndef __ST7102_H
#define __ST7102_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "touch.h"
#include "string.h"
#include "myiic.h"


/* 触摸芯片引脚相关定义 */
#define ST7102_INT_GPIO_PIN             GPIO_NUM_15
#define ST7102_RST_GPIO_PIN             GPIO_NUM_18

#define ST7102_INT                      gpio_get_level(ST7102_INT_GPIO_PIN)

/* 触摸屏复位引脚控制 */
#define ST7102_RST(x)   do { x ?                                      \
                             gpio_set_level(ST7102_RST_GPIO_PIN, 1) : \
                             gpio_set_level(ST7102_RST_GPIO_PIN, 0);  \
                        } while(0)

/* ST7102 I2C 从机地址(7位). 命令0xAA(写)/0xAB(读)是含读写位的8位形式, 右移1位即0x55 */
#define ST7102_DEV_ID                   0x55        /* ST7102设备地址(7位) */

/* ST7102 部分寄存器定义 */
#define ST7102_FW_VERSION_REG           0X0000      /* 固件版本寄存器 */
#define ST7102_STATUS_REG               0X0001      /* 状态寄存器 */
#define ST7102_DEVICE_CTRL_REG          0X0002      /* 设备控制寄存器 */
#define ST7102_MAX_X_H_REG              0X0005      /* X坐标最大值高字节 */
#define ST7102_MAX_X_L_REG              0X0006      /* X坐标最大值低字节 */
#define ST7102_MAX_Y_H_REG              0X0007      /* Y坐标最大值高字节 */
#define ST7102_MAX_Y_L_REG              0X0008      /* Y坐标最大值低字节 */
#define ST7102_MAX_TOUCH_REG            0X0009      /* 最大触摸点数 */
#define ST7102_FW_REVISION_REG          0X000C      /* 固件修订号 */
#define ST7102_ADV_TOUCH_INFO_REG       0X0010      /* 触摸信息寄存器 */
#define ST7102_GESTURE_REG              0X0012      /* 手势寄存器 */
#define ST7102_KEYS_REG                 0X0013      /* 按键寄存器 */

#define ST7102_TP1_REG                  0X0014      /* 第一个触摸点数据地址 */
#define ST7102_TP2_REG                  0X001B      /* 第二个触摸点数据地址 */
#define ST7102_TP3_REG                  0X0022      /* 第三个触摸点数据地址 */
#define ST7102_TP4_REG                  0X0029      /* 第四个触摸点数据地址 */
#define ST7102_TP5_REG                  0X0030      /* 第五个触摸点数据地址 */
#define ST7102_TP6_REG                  0X0037      /* 第六个触摸点数据地址 */
#define ST7102_TP7_REG                  0X003E      /* 第七个触摸点数据地址 */
#define ST7102_TP8_REG                  0X0045      /* 第八个触摸点数据地址 */
#define ST7102_TP9_REG                  0X004C      /* 第九个触摸点数据地址 */
#define ST7102_TP10_REG                 0X0053      /* 第十个触摸点数据地址 */

#define ST7102_POINT_DATA_LEN           7           /* 每个触摸点数据长度(字节) */
#define ST7102_WITH_COORD               0X08        /* 触摸信息寄存器: 有坐标更新标志 */
#define ST7102_VALID_MASK               0X80        /* 触摸点有效标志 */
#define ST7102_INT_ACTIVE               0           /* INT引脚低电平表示有触摸事件 */

/* 函数声明 */
esp_err_t st7102_init(void);                                        /* 初始化ST7102触摸屏 */
uint8_t st7102_scan(uint8_t mode);                                  /* 扫描触摸屏 */
esp_err_t st7102_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len);   /* 向ST7102写入数据 */
esp_err_t st7102_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len);   /* 从ST7102读出数据 */

#endif
