/**
 ******************************************************************************
 * @file        my_spiffs.h
 * @version     V1.0
 * @brief       SPIFFS驱动测试代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#ifndef __MY_SPIFFS_H
#define __MY_SPIFFS_H

#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_log.h"

#define DEFAULT_FD_NUM          5           /* 默认最大可打开文件数量 */
#define DEFAULT_MOUNT_POINT     "/spiffs"   /* 文件系统名称 */

/* 函数声明 */
esp_err_t spiffs_init(char *partition_label, char *mount_point, size_t max_files);      /* spiffs初始化 */

#endif
