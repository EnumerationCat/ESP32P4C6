#include "sdmmc.h"

#include <stdbool.h>
#include <stdlib.h>

const char* sdmmc_tag = "sdmmc";
sdmmc_card_t *card = NULL;
const char mount_point[] = MOUNT_POINT;
sdmmc_mount_state_t sdmmc_mount_state = SDMMC_MOUNT_STATE_UNMOUNTED;

static bool s_phy_powered = false;      /* LDO只申请一次, 切模式时不需要重复申请 */


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
    if (sdmmc_mount_state == SDMMC_MOUNT_STATE_MOUNTED && card != NULL)
    {
        sdmmc_unmount();    /* 取消挂载 */
    }
    else if (sdmmc_mount_state == SDMMC_MOUNT_STATE_RAW)
    {
        sdmmc_deinit_raw(); /* U盘模式退出: 先拆掉裸模式 */
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
        sdmmc_mount_state = SDMMC_MOUNT_STATE_FAILED;
        return ESP_FAIL;
    }

    /* 打印当前TF卡相关信息 */
    sdmmc_card_print_info(stdout, card);
    sdmmc_mount_state = SDMMC_MOUNT_STATE_MOUNTED;
    return ESP_OK;
}

/**
 * @brief       取消挂载SD卡
 * @note        esp_vfs_fat_sdcard_unmount 会释放card并反初始化SDMMC主机,
 *              之后要再用卡必须先 sdmmc_init() 或 sdmmc_init_raw()。
 * @param       无
 * @retval      ESP_OK:卸载成功
 */
esp_err_t sdmmc_unmount(void)
{
    if (card == NULL)
    {
        sdmmc_mount_state = SDMMC_MOUNT_STATE_UNMOUNTED;
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point, card);

    card = NULL;        /* 卡结构已在上面的函数中被释放 */

    if (ret != ESP_OK)
    {
        ESP_LOGE(sdmmc_tag, "unmount failed: %s", esp_err_to_name(ret));
        sdmmc_mount_state = SDMMC_MOUNT_STATE_FAILED;
        return ret;
    }

    sdmmc_mount_state = SDMMC_MOUNT_STATE_UNMOUNTED;
    return ESP_OK;
}

/**
 * @brief       初始化SD卡但不挂载FATFS
 * @note        供USB MSC(U盘)模式使用: 卡由TinyUSB直接按扇区读写。
 *              若当前已挂载FATFS会先卸载。
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t sdmmc_init_raw(void)
{
    esp_err_t ret;

    if (sdmmc_mount_state == SDMMC_MOUNT_STATE_RAW && card != NULL)
    {
        return ESP_OK;
    }

    if (sdmmc_mount_state == SDMMC_MOUNT_STATE_MOUNTED)
    {
        ret = sdmmc_unmount();
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    sd_dev_bsp_enable_phy_power();

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    ret = host.init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(sdmmc_tag, "host init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk   = SDMMC_PIN_CLK;
    slot_config.cmd   = SDMMC_PIN_CMD;
    slot_config.d0    = SDMMC_PIN_D0;
    slot_config.d1    = SDMMC_PIN_D1;
    slot_config.d2    = SDMMC_PIN_D2;
    slot_config.d3    = SDMMC_PIN_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ret = sdmmc_host_init_slot(host.slot, &slot_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(sdmmc_tag, "host init slot failed: %s", esp_err_to_name(ret));
        host.deinit();
        return ret;
    }

    card = (sdmmc_card_t *)calloc(1, sizeof(sdmmc_card_t));
    if (card == NULL)
    {
        ESP_LOGE(sdmmc_tag, "no memory for card");
        sdmmc_host_deinit();
        return ESP_ERR_NO_MEM;
    }

    ret = sdmmc_card_init(&host, card);
    if (ret != ESP_OK)
    {
        ESP_LOGE(sdmmc_tag, "card init failed: %s", esp_err_to_name(ret));
        free(card);
        card = NULL;
        sdmmc_host_deinit();
        sdmmc_mount_state = SDMMC_MOUNT_STATE_FAILED;
        return ret;
    }

    sdmmc_card_print_info(stdout, card);
    sdmmc_mount_state = SDMMC_MOUNT_STATE_RAW;
    return ESP_OK;
}

/**
 * @brief       反初始化裸模式(USB MSC退出时调用)
 * @param       无
 * @retval      ESP_OK:成功
 */
esp_err_t sdmmc_deinit_raw(void)
{
    if (sdmmc_mount_state != SDMMC_MOUNT_STATE_RAW)
    {
        return ESP_OK;
    }

    sdmmc_host_deinit();

    free(card);
    card = NULL;
    sdmmc_mount_state = SDMMC_MOUNT_STATE_UNMOUNTED;
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
    if (s_phy_powered)
    {
        return;
    }

    esp_ldo_channel_config_t ldo_sd_phy_config = {
        .chan_id    = SD_PHY_PWR_LDO_CHAN,        /* 选择内存LDO */
        .voltage_mv = SD_PHY_PWR_LDO_VOLTAGE_MV,  /* 输出标准电压提供VDD_SD_DPHY */
    };

    if (esp_ldo_acquire_channel(&ldo_sd_phy_config, &ldo_sd_phy) == ESP_OK)
    {
        s_phy_powered = true;
        ESP_LOGI(sdmmc_tag, "SD PHY Powered on");
    }
    else
    {
        ESP_LOGE(sdmmc_tag, "SD PHY power on failed");
    }
#endif
}
