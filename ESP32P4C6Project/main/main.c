#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "touch.h"
#include "led.h"
#include "lcd.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_port_fs.h"
#include "user_home.h"
#include "app_shell.h"
#include "wifi_manager.h"
#include "ap_wifi.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"
#include "mipi_cam.h"
#include "sdmmc.h"
#include "flash_fat.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

static const char *TAG = "MAIN";

/* 要连接的Wi-Fi，改成自己的 */
#define WIFI_SSID       "Wi-Fi"
#define WIFI_PASSWORD   "12345678"

#define WIFI_CONNECTED_BIT  BIT0
static EventGroupHandle_t wifi_ev = NULL;

/**
 * @brief       WiFi连接状态回调
 * @param       state: 连接状态
 * @retval      无
 */
static void wifi_state_handler(WIFI_STATE state)
{
    if (state == WIFI_STATE_CONNECTED)
    {
        ESP_LOGI(TAG, "WIFI_STATE_CONNECTED");
        xEventGroupSetBits(wifi_ev, WIFI_CONNECTED_BIT);
    }
    else if (state == WIFI_STATE_DISCONNECTED)
    {
        ESP_LOGI(TAG, "WIFI_STATE_DISCONNECTED");
        xEventGroupClearBits(wifi_ev, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief       相机按钮回调(用户界面无返回值)
 * @param       无
 * @retval      无
 */
static void camera_btn_handler(void)
{
    mipi_cam_preview_start();
}

/**
 * @brief       LVGL任务
 * @param       arg: 未使用
 * @retval      无
 */
static void lvgl_task(void *arg)
{
    (void)arg;

    lv_init();                          /* 初始化LVGL */
    lv_port_disp_init();                /* 注册显示驱动 */
    lv_port_indev_init();               /* 注册触摸输入 */
    lv_port_fs_init();                  /* 注册文件系统驱动: D: = /User(SD卡), C: = /Factory(Flash FAT) */

    lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE),
                          lv_palette_main(LV_PALETTE_RED),
                          LV_THEME_DEFAULT_DARK, LV_FONT_DEFAULT);

    user_home();                        /* 显示主界面 */

    bool preview_was_active = false;

    while (1)
    {
        /* 摄像头全屏预览期间暂停LVGL刷新, 避免和摄像头抢LCD帧缓冲 */
        if (mipi_cam_preview_active())
        {
            preview_was_active = true;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (preview_was_active)
        {
            preview_was_active = false;
            lv_obj_invalidate(lv_scr_act());    /* 退出预览后整屏重绘, 恢复界面 */
        }

        lv_timer_handler();
        app_shell_tick();       /* 把串口shell输出刷到LCD文本页 */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/**
 * @brief       程序入口
 * @param       无
 * @retval      无
 */
void app_main(void)
{
    esp_err_t ret;
    bool onenet_running = false;

    ret = nvs_flash_init();     /* 初始化NVS */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    app_shell_init();           /* 串口命令行(UART0): help/hello/free/clear/reboot, 输出镜像到LCD */

    sdmmc_init();               /* 挂载SD卡: LVGL文件系统盘符 D: -> /User */
    flash_fat_init();           /* 挂载Flash FAT分区: LVGL文件系统盘符 C: -> /Factory */

    led_init();                 /* LED初始化 */
    lcd_init();                 /* LCD屏初始化 */
    tp_dev.init();              /* 初始化触摸屏(同时初始化I2C总线) */

    /* 摄像头只做初始化, 点击界面上的相机按钮后再开始全屏预览 */
    if (mipi_cam_init() != ESP_OK)
    {
        ESP_LOGW(TAG, "mipi camera init failed");
    }
    user_home_set_camera_cb(camera_btn_handler);

    /* WiFi初始化并连接热点 */
    onenet_dm_init();
    wifi_ev = xEventGroupCreate();
    ap_wifi_init(wifi_state_handler);
    wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD);

    /* 主任务栈只有3584字节, LVGL放到独立任务中运行 */
    xTaskCreate(lvgl_task, "lvgl", 8 * 1024, NULL, 4, NULL);
    /* WiFi连上后启动OneNET；断网时停止，重连后再次启动。
     * 注意事件位要用 clear-on-exit，否则连上后等待会立即返回形成忙等循环；
     * onenet_running 用于保证只创建一次MQTT客户端，避免重复启动耗尽内存 */
    while (1)
    {
        xEventGroupWaitBits(wifi_ev, WIFI_CONNECTED_BIT, pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(3000));

        if (wifi_manager_is_connect())
        {
            if (!onenet_running && onenet_start() == ESP_OK)
            {
                onenet_running = true;
                ESP_LOGI(TAG, "onenet started");
            }
        }
        else if (onenet_running)
        {
            onenet_stop();
            onenet_running = false;
            ESP_LOGW(TAG, "wifi lost, onenet stopped");
        }
    }
}
