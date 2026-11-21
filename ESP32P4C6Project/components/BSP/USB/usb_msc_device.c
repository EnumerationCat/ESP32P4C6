/**
 * @file usb_msc_device.c
 * @brief USB MSC 设备模式实现 (esp_tinyusb 3.x)
 *
 * TinyUSB在ESP32-P4上使用高速USB OTG口(RHPORT_HS), 与项目中USB Host鼠标
 * 使用的是同一个物理口, 因此两种模式互斥, 切换时必须先卸载对方。
 *
 * 设备描述符使用 esp_tinyusb 的默认描述符(根据Kconfig里启用的MSC类生成),
 * 这样不用手写配置描述符, 也不会随TinyUSB版本变化失效。
 */

#include "usb_msc_device.h"
#include "sdmmc.h"
#include "usb_hid_mouse.h"
#include "videoplay.h"

#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

#include "esp_log.h"

static const char *TAG = "usb_msc";

static bool s_active = false;
static tinyusb_msc_storage_handle_t s_storage = NULL;

/**
 * @brief       TinyUSB设备事件日志: 用于确认PC是否检测到设备
 * @param       event: 事件数据
 * @param       arg: 用户参数(未使用)
 * @retval      无
 */
static void usb_device_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;

    switch (event->id)
    {
        case TINYUSB_EVENT_ATTACHED:
            ESP_LOGI(TAG, "device attached to host");
            break;
        case TINYUSB_EVENT_DETACHED:
            ESP_LOGW(TAG, "device detached from host");
            break;
        case TINYUSB_EVENT_SUSPENDED:
            ESP_LOGI(TAG, "device suspended by host");
            break;
        case TINYUSB_EVENT_RESUMED:
            ESP_LOGI(TAG, "device resumed by host");
            break;
        default:
            break;
    }
}

/**
 * @brief       失败时回滚到正常模式(重新挂载SD卡和USB鼠标)
 * @param       无
 * @retval      无
 */
static void restore_normal_mode(void)
{
    tinyusb_driver_uninstall();

    if (s_storage != NULL)
    {
        tinyusb_msc_delete_storage(s_storage);
        s_storage = NULL;
    }

    tinyusb_msc_uninstall_driver();
    sdmmc_deinit_raw();

    esp_err_t ret = sdmmc_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "restore SD mount failed: %s", esp_err_to_name(ret));
    }

    ret = usb_hid_mouse_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "restore USB mouse failed: %s", esp_err_to_name(ret));
    }
}

esp_err_t usb_msc_device_start(void)
{
    if (s_active)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (videoplay_active())
    {
        ESP_LOGE(TAG, "video is playing, stop it first");
        return ESP_ERR_INVALID_STATE;
    }

    /* 1. 同一个USB口不能同时做Host和Device */
    esp_err_t ret = usb_hid_mouse_deinit();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "USB mouse deinit failed: %s", esp_err_to_name(ret));
    }

    /* 2. 卸掉FATFS, 按裸卡初始化, 交给TinyUSB直接读写扇区 */
    ret = sdmmc_init_raw();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD raw init failed: %s", esp_err_to_name(ret));
        usb_hid_mouse_init();
        return ret;
    }

    /* 3. 安装MSC驱动并注册SD卡存储, 卡归USB主机(PC)所有 */
    const tinyusb_msc_driver_config_t driver_config = {
        .user_flags.auto_mount_off = 1,     /* 不自动在ESP侧挂载, 卡只给PC用 */
        .callback = NULL,
        .callback_arg = NULL,
    };

    ret = tinyusb_msc_install_driver(&driver_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "MSC driver install failed: %s", esp_err_to_name(ret));
        restore_normal_mode();
        return ret;
    }

    const tinyusb_msc_storage_config_t storage_config = {
        .medium.card = card,
        .fat_fs = {
            .base_path = NULL,
            .config.max_files = 5,
            .do_not_format = true,          /* 没有FAT时不要自动格式化 */
            .format_flags = 0,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };

    ret = tinyusb_msc_new_storage_sdmmc(&storage_config, &s_storage);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "MSC storage create failed: %s", esp_err_to_name(ret));
        restore_normal_mode();
        return ret;
    }

    /* 4. 安装TinyUSB设备协议栈(默认配置: P4上就是高速OTG口) */
    const tinyusb_config_t tusb_config = TINYUSB_DEFAULT_CONFIG(usb_device_event_cb);

    ret = tinyusb_driver_install(&tusb_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(ret));
        restore_normal_mode();
        return ret;
    }

    s_active = true;
    ESP_LOGI(TAG, "U-disk mode ON: connect PC to read/write the SD card");
    return ESP_OK;
}

esp_err_t usb_msc_device_stop(void)
{
    if (!s_active)
    {
        return ESP_OK;
    }

    esp_err_t ret = tinyusb_driver_uninstall();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "TinyUSB uninstall failed: %s", esp_err_to_name(ret));
    }

    if (s_storage != NULL)
    {
        esp_err_t err = tinyusb_msc_delete_storage(s_storage);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "MSC storage delete failed: %s", esp_err_to_name(err));
        }
        s_storage = NULL;
    }

    tinyusb_msc_uninstall_driver();
    sdmmc_deinit_raw();

    /* 恢复FATFS挂载和USB鼠标 */
    ret = sdmmc_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD mount failed after U-disk mode: %s", esp_err_to_name(ret));
    }

    esp_err_t mouse_ret = usb_hid_mouse_init();
    if (mouse_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "USB mouse init failed: %s", esp_err_to_name(mouse_ret));
    }

    s_active = false;
    ESP_LOGI(TAG, "U-disk mode OFF");
    return ret;
}

bool usb_msc_device_active(void)
{
    return s_active;
}
