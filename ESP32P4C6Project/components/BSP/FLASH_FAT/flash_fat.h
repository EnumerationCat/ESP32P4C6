#ifndef __FLASH_FAT_H
#define __FLASH_FAT_H

#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "esp_err.h"
#include "esp_log.h"

/* Flash FAT分区(分区表: vfs, data, fat, 0x200000, 10MB) */
#define FLASH_FAT_PARTITION_LABEL   "vfs"

/* 挂载点(不能与SDMMC的MOUNT_POINT重名, 否则同时包含两个头文件会宏重定义) */
#define FACTORY_MOUNT_POINT         "/Factory"

/* Flash FAT挂载状态 */
typedef enum
{
    FLASH_FAT_MOUNT_STATE_UNMOUNTED = 0,    /* 未挂载 */
    FLASH_FAT_MOUNT_STATE_MOUNTED,          /* 已挂载 */
    FLASH_FAT_MOUNT_STATE_FAILED,           /* 挂载失败 */
} flash_fat_mount_state_t;

/* 外部变量声明 */
extern wl_handle_t flash_fat_wl_handle;     /* 磨损均衡句柄 */
extern flash_fat_mount_state_t flash_fat_mount_state; /* Flash FAT挂载状态 */

/* 函数声明 */
esp_err_t flash_fat_init(void);             /* Flash FAT分区初始化并挂载 */
esp_err_t flash_fat_unmount(void);          /* 取消挂载Flash FAT分区 */

#endif
