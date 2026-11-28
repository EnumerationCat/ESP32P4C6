/**
 * @file wifi_event_service.h
 * @brief WiFi 状态/扫描完成 -> app_event 的桥接服务
 *
 * 负责:
 *  - 初始化 WiFi 协议栈(ap_wifi_init + 扫描结果 Model);
 *  - 把连接状态变化转成 "wifi_connected"/"wifi_disconnected";
 *  - 把扫描完成写入 wifi_scan_model 并投递 "wifi_scan_done"。
 * 页面只发扫描命令(wifi_event_service_scan)并订阅事件。
 */

#ifndef WIFI_EVENT_SERVICE_H
#define WIFI_EVENT_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       初始化 WiFi 协议栈与事件桥接(幂等, 在 pages_init_all 中调用)
 * @note        内部执行 esp_netif_init, 后续 SD/FAT 挂载依赖它
 * @param       无
 * @retval      无
 */
void wifi_event_service_init(void);

/**
 * @brief       发起一次扫描(结果写入 Model 并投递 "wifi_scan_done")
 * @param       无
 * @retval      ESP_OK: 已启动; 其他: 扫描任务创建失败等
 */
esp_err_t wifi_event_service_scan(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_EVENT_SERVICE_H */
