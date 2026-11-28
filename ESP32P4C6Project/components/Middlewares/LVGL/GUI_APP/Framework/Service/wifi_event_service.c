/**
 * @file wifi_event_service.c
 * @brief WiFi 状态/扫描完成 -> app_event 桥接实现
 *
 * 两个 bridge 分别运行在 wifi 扫描任务 / esp_event 任务中, 只写 Model
 * 并投递事件, 不触碰 LVGL。
 */

#include "wifi_event_service.h"
#include "app_event.h"
#include "wifi_scan_model.h"
#include "wifi_manager.h"
#include "ap_wifi.h"

#include <stdbool.h>

/* 扫描完成(扫描任务上下文): 先把结果写进 Model, 再通知 LVGL 任务上屏 */
static void wifi_scan_bridge(int num, wifi_ap_record_t *records)
{
    wifi_scan_model_on_result(num, records);
    app_event_post("wifi_scan_done", 0);
}

/* 连接状态变化(esp_event 任务上下文) */
static void wifi_state_bridge(WIFI_STATE state)
{
    app_event_post(state == WIFI_STATE_CONNECTED ? "wifi_connected"
                                                 : "wifi_disconnected", 0);
}

void wifi_event_service_init(void)
{
    static bool inited = false;

    if (inited)
    {
        return;
    }

    inited = true;

    /* 启动 STA 但不连接任何热点, 由页面选择; 状态变化经 app_event 投递 */
    wifi_scan_model_init();
    ap_wifi_init(wifi_state_bridge);
}

esp_err_t wifi_event_service_scan(void)
{
    return wifi_manager_scan(wifi_scan_bridge);
}
