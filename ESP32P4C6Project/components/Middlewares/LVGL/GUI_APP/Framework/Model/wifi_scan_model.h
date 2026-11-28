/**
 * @file wifi_scan_model.h
 * @brief WiFi扫描数据层(model): 扫描结果的接收入口与缓存
 *
 * 分层约定:
 *  - 本层不依赖LVGL, 不调用任何 lv_* 接口;
 *  - 扫描回调(在扫描任务中)调用 wifi_scan_model_on_result() 写入缓存;
 *  - 显示层(view, LVGL任务)在就绪后调用 wifi_scan_model_take_options()
 *    取走已拼好的下拉框字符串。
 *
 * 线程模型: 扫描任务写, LVGL任务读, list/count/options 由内部互斥锁保护;
 * busy/ready 为 volatile 标志, 与 scan任务/界面任务之间的状态握手。
 */

#ifndef WIFI_SCAN_MODEL_H
#define WIFI_SCAN_MODEL_H

#include <stdbool.h>

#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_AP             20                          /* 最多缓存的热点数 */
#define WIFI_SSID_LEN           33                          /* SSID 最大长度(含结束符) */
#define WIFI_OPTIONS_LEN        (WIFI_MAX_AP * WIFI_SSID_LEN + 1)

/**
 * @brief       创建互斥锁(幂等, 在页面初始化时调用)
 * @param       无
 * @retval      无
 */
void wifi_scan_model_init(void);

/**
 * @brief       开始一次扫描: 置 busy, 清 ready
 * @param       无
 * @retval      true:可以发起扫描; false:已有扫描在进行中
 */
bool wifi_scan_model_begin(void);

/**
 * @brief       放弃本次扫描(发起失败或超时), 清 busy
 * @param       无
 * @retval      无
 */
void wifi_scan_model_timeout(void);

/**
 * @brief       扫描进行中?
 * @param       无
 * @retval      true:yes
 */
bool wifi_scan_model_is_busy(void);

/**
 * @brief       有新结果待取?
 * @param       无
 * @retval      true:yes
 */
bool wifi_scan_model_is_ready(void);

/**
 * @brief       丢弃待取结果(页面销毁时调用)
 * @param       无
 * @retval      无
 */
void wifi_scan_model_clear_ready(void);

/**
 * @brief       扫描完成回调写入点(运行在扫描任务中)
 * @note        拷贝SSID(跳过隐藏SSID)并拼好 options 换行串, 置 ready、清 busy
 * @param       num: 热点数
 * @param       records: 热点记录数组
 * @retval      无
 */
void wifi_scan_model_on_result(int num, wifi_ap_record_t *records);

/**
 * @brief       取走扫描结果字符串(仅LVGL任务)
 * @note        返回内部 options 缓冲指针, 取走后 ready 清零。
 *              指针在下一次扫描开始前有效; 调用方(下拉框)应立即复制使用。
 * @param       count: 输出热点数, 可为NULL
 * @retval      换行拼接的SSID串; 无待取结果返回NULL
 */
const char *wifi_scan_model_take_options(int *count);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_SCAN_MODEL_H */
