/**
 ******************************************************************************
 * @file        main.c
 * @version     V1.0
 * @brief       触摸屏 + WiFi + OneNET 实验
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:     慧勤智远 ESP32-P4 开发板
 ******************************************************************************
 */

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
#include "user_home.h"
#include "wifi_manager.h"
#include "ap_wifi.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"
#include <stdio.h>
#include <stdbool.h>

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

    lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE),
                          lv_palette_main(LV_PALETTE_RED),
                          LV_THEME_DEFAULT_DARK, LV_FONT_DEFAULT);

    user_home();                        /* 显示主界面 */

    while (1)
    {
        lv_timer_handler();
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

    led_init();                 /* LED初始化 */
    lcd_init();                 /* LCD屏初始化 */
    tp_dev.init();              /* 初始化触摸屏 */

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
