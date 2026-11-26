#ifndef __MIPI_CAM_H
#define __MIPI_CAM_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "app_video.h"
#include "i2c_bus.h"
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
i2c_master_bus_handle_t mipi_cam_sccb_bus(void); /* 摄像头SCCB的I2C总线(与ES8311共用),首次调用时创建 */

/* --- 捕获模式: 供 LVGL 页面把画面当 lv_img 数据源显示 ---
 * 与 preview_start() 互斥. 画面经 PPA 裁剪/缩放到面板分辨率、RGB565,
 * 写入调用者的缓冲(可直接作为 LV_IMG_CF_TRUE_COLOR 数据源).
 * 缓冲要求: 按 mipi_cam_frame_buf_align() 对齐, 大小 >= mipi_cam_frame_buf_size().
 * 典型流程: begin -> 循环 read -> end, 在同一个任务里调用. */
esp_err_t mipi_cam_frame_begin(void);                       /* 开流, 进入捕获模式 */
esp_err_t mipi_cam_frame_read(void *dst, size_t dst_size);  /* 取一帧写入dst(开始新的一帧需先begin) */
void      mipi_cam_frame_end(void);                         /* 关流, 退出捕获模式 */
bool      mipi_cam_frame_size(uint16_t *w, uint16_t *h);    /* 输出图像尺寸(即面板分辨率) */
size_t    mipi_cam_frame_buf_size(void);                    /* 单帧缓冲建议大小(已按cache行对齐) */
size_t    mipi_cam_frame_buf_align(void);                   /* 缓冲需要的对齐字节数 */

#endif
