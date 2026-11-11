/**
 ******************************************************************************
 * @file        sdmmc.c
 * @version     V1.0
 * @brief       TF驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

#include "sdmmc.h"

const char* sdmmc_tag = "sdmmc";
sdmmc_card_t *card = NULL;
const char mount_point[] = MOUNT_POINT;
uint8_t sdmmc_mount_flag = 0x00;


/**
 * @brief       SD卡初始化并挂载SD卡
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t sdmmc_init(void)
{
	sd_dev_bsp_enable_phy_power();    /* 配置SDIO接口电压3.3V */

    esp_err_t ret = ESP_OK;
    
    /* 防止多次调用这个函数初始化 */
    if (sdmmc_mount_flag == 0x01 && card != NULL)
    {
        sdmmc_unmount();    /* 取消挂载 */
    }
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,        /* 挂载FAT分区 */
        .max_files              = 5,            /* 打开分文件最大数量 */
        .allocation_unit_size   = 16 * 1024     /* 分配单位大小 */
    };

    sdmmc_host_t sdmmc_host = SDMMC_HOST_DEFAULT();     /* SDMMC控制器默认设置,20MHz通信频率 */
    sdmmc_host.slot = SDMMC_HOST_SLOT_0;                /* Slot1被ESP-Hosted的SDIO占用, TF卡必须用Slot0 */
    sdmmc_host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;     /* 设置速度最大值为40MHz */

    sdmmc_slot_config_t sdmmc_config = SDMMC_SLOT_CONFIG_DEFAULT(); /* SDMMC控制器硬件相关设置,总线宽度为4位 */
    sdmmc_config.width = 4;                 /* SDMMC总线宽度为4 */
    sdmmc_config.clk   = SDMMC_PIN_CLK;     /* SDMMC的时钟引脚 */
    sdmmc_config.cmd   = SDMMC_PIN_CMD;     /* SDMMC的命令引脚 */
    sdmmc_config.d0    = SDMMC_PIN_D0;      /* SDMMC的数据0引脚 */
    sdmmc_config.d1    = SDMMC_PIN_D1;      /* SDMMC的数据1引脚 */
    sdmmc_config.d2    = SDMMC_PIN_D2;      /* SDMMC的数据2引脚 */
    sdmmc_config.d3    = SDMMC_PIN_D3;      /* SDMMC的数据3引脚 */
    sdmmc_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;  /* 内部弱上拉 */

    ret = esp_vfs_fat_sdmmc_mount(mount_point, &sdmmc_host, &sdmmc_config, &mount_config, &card);   /* 挂载文件系统(内部会调用sdmmc_host_init_slot初始化SDMMC) */
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(sdmmc_tag, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            ESP_LOGE(sdmmc_tag, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        sdmmc_mount_flag = 0xFF;
        return ESP_FAIL;
    }

    /* 打印当前TF卡相关信息 */
    sdmmc_card_print_info(stdout, card);
    sdmmc_mount_flag = 0x01;
    return ESP_OK;
}

/**
 * @brief       取消挂载SD卡
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t sdmmc_unmount(void)
{
    ESP_ERROR_CHECK(esp_vfs_fat_sdcard_unmount(mount_point, card));
    sdmmc_mount_flag = 0x00;
    return ESP_OK;
}

/**
 * @brief       配置sd phy电压
 * @param       无
 * @retval      无
 */
void sd_dev_bsp_enable_phy_power(void)
{
    esp_ldo_channel_handle_t ldo_sd_phy = NULL;
#ifdef SD_PHY_PWR_LDO_CHAN
    esp_ldo_channel_config_t ldo_sd_phy_config = {
        .chan_id    = SD_PHY_PWR_LDO_CHAN,        /* 选择内存LDO */
        .voltage_mv = SD_PHY_PWR_LDO_VOLTAGE_MV,  /* 输出标准电压提供VDD_SD_DPHY */
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_sd_phy_config, &ldo_sd_phy));
    ESP_LOGI(sdmmc_tag, "SD PHY Powered on");
#endif
}
