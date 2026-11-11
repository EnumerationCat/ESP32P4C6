#include "flash_fat.h"

const char *flash_fat_tag = "flash_fat";
wl_handle_t flash_fat_wl_handle = WL_INVALID_HANDLE;
flash_fat_mount_state_t flash_fat_mount_state = FLASH_FAT_MOUNT_STATE_UNMOUNTED;

/**
 * @brief       Flash FAT分区初始化并挂载(带磨损均衡)
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t flash_fat_init(void)
{
    esp_err_t ret = ESP_OK;

    /* 防止多次调用这个函数初始化 */
    if (flash_fat_mount_state == FLASH_FAT_MOUNT_STATE_MOUNTED)
    {
        flash_fat_unmount();    /* 取消挂载 */
    }

    esp_vfs_fat_mount_config_t mount_config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount_config.format_if_mount_failed = true;     /* 分区首次使用为空, 挂载失败时格式化 */
    mount_config.max_files              = 5;        /* 最大可同时打开的文件数 */
    mount_config.allocation_unit_size   = 4 * 1024; /* 磨损均衡扇区为4KB, 分配单元必须为扇区~128倍扇区的2的幂 */

    /* 挂载Flash上的FAT分区(内部会调用wl_mount建立磨损均衡) */
    ret = esp_vfs_fat_spiflash_mount_rw_wl(FACTORY_MOUNT_POINT, FLASH_FAT_PARTITION_LABEL,
                                           &mount_config, &flash_fat_wl_handle);
    if (ret != ESP_OK)
    {
        if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(flash_fat_tag, "Failed to find FAT partition \"%s\"", FLASH_FAT_PARTITION_LABEL);
        }
        else if (ret == ESP_FAIL)
        {
            ESP_LOGE(flash_fat_tag, "Failed to mount or format filesystem");
        }
        else
        {
            ESP_LOGE(flash_fat_tag, "Failed to initialize Flash FAT (%s)", esp_err_to_name(ret));
        }

        flash_fat_mount_state = FLASH_FAT_MOUNT_STATE_FAILED;
        return ESP_FAIL;
    }

    /* 打印当前FAT分区容量信息 */
    uint64_t total = 0;
    uint64_t free_bytes = 0;
    ret = esp_vfs_fat_info(FACTORY_MOUNT_POINT, &total, &free_bytes);
    if (ret != ESP_OK)
    {
        ESP_LOGI(flash_fat_tag, "Failed to get FAT partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(flash_fat_tag, "Partition size: total: %llu Bytes, free: %llu Bytes",
                 (unsigned long long)total, (unsigned long long)free_bytes);
    }

    flash_fat_mount_state = FLASH_FAT_MOUNT_STATE_MOUNTED;
    return ESP_OK;
}

/**
 * @brief       取消挂载Flash FAT分区
 * @param       无
 * @retval      ESP_OK:成功
 */
esp_err_t flash_fat_unmount(void)
{
    ESP_ERROR_CHECK(esp_vfs_fat_spiflash_unmount_rw_wl(FACTORY_MOUNT_POINT, flash_fat_wl_handle));
    flash_fat_wl_handle = WL_INVALID_HANDLE;
    flash_fat_mount_state = FLASH_FAT_MOUNT_STATE_UNMOUNTED;
    return ESP_OK;
}
