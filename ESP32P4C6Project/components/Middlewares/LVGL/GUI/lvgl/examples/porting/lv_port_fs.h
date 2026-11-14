/**
 * @file lv_port_fs.h
 * @brief LVGL 文件系统适配: 基于 ESP-IDF VFS(FatFs) 的 POSIX 接口
 */

#ifndef LV_PORT_FS_H
#define LV_PORT_FS_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl/lvgl.h"

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief       向LVGL注册文件系统驱动
 * @note        必须在 lv_init() 之后调用; BSP的挂载(sdmmc_init()/flash_fat_init())需在此之前完成
 * @param       无
 * @retval      无
 */
void lv_port_fs_init(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_FS_H*/
