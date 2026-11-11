#ifndef __MIPI_CAM_H
#define __MIPI_CAM_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include "app_video.h"
#include "myiic.h"
#include "driver/ppa.h"
#include "esp_timer.h"
#include "lcd.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/errno.h>
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"


/* 函数声明 */
esp_err_t mipi_cam_init(void);              /* mipi_cam初始化(只初始化硬件,不显示) */
esp_err_t mipi_cam_preview_start(void);     /* 请求全屏预览,由摄像头任务接管LCD */
bool mipi_cam_preview_active(void);         /* 预览是否进行中(LVGL据此暂停刷新) */

#endif
