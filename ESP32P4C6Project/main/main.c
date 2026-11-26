#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "hardware_init.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_port_fs.h"
#include "user_home_page.h"
#include "videoplay.h"
#include "page_manager.h"
#include "page_init.h"
#include "camera_page.h"
#include "wifi_manager.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"
#include "mipi_cam.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

static const char *TAG = "MAIN";

/**
 * @brief       相机按钮回调(用户界面无返回值)
 * @note        进入相机页: 画面由 camera_page 通过 lv_img 显示, 可叠加返回按钮
 * @param       无
 * @retval      无
 */
static void camera_btn_handler(void)
{
    manager_gopage(CAMERA_PAGE_ID);
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

    manager_gopage(USER_HOME_PAGE_ID);  /* 显示主界面 */

    bool preview_was_active = false;

    while (1)
    {
        /* 摄像头/视频全屏播放期间暂停LVGL刷新, 避免和它们抢LCD帧缓冲 */
        if (mipi_cam_preview_active() || videoplay_active())
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
        page_manager_tick();    /* 只刷新当前可见页(把串口shell输出刷到LCD文本页) */
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

    /* 注册所有页面: shell命令行、主界面、视频、U盘、录音/回放、WiFi;
     * wifi 页会执行 esp_netif_init, 所以必须先于硬件初始化 */
    pages_init_all();

    /* 板级硬件按依赖顺序初始化: SD/FAT 挂载、LED、LCD、触摸、USB鼠标、摄像头、音频;
     * 单项失败只警告, 不阻塞其它功能 */
    hardware_init_all();

    /* 摄像头只做初始化, 点击界面上的相机按钮后再开始全屏预览 */
    user_home_page_set_camera_cb(camera_btn_handler);

    /* WiFi协议栈由WiFi页面模块初始化; 连接哪个热点也在WiFi页面里选择 */
    onenet_dm_init();

    /* 主任务栈只有3584字节, LVGL放到独立任务中运行 */
    xTaskCreate(lvgl_task, "lvgl", 8 * 1024, NULL, 4, NULL);
    /* WiFi连上后启动OneNET；断网时停止，重连后再次启动。
     * onenet_running 用于保证只创建一次MQTT客户端，避免重复启动耗尽内存 */
    while (1)
    {
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

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
