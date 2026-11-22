/**
 * @file wifi_page.h
 * @brief WiFi连接页: 扫描热点后从下拉框选择, 输入密码并连接
 *
 * 页面本身是 view_t, 与其它页面使用同一套 page_manager 生命周期。
 * 扫描/连接复用 wifi_manager 的接口, 扫描结果只在 LVGL 任务中写入控件。
 */

#ifndef WIFI_PAGE_H
#define WIFI_PAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_PAGE_ID    7       /* 页面ID(page_manager注册表主键) */

/**
 * @brief       注册WiFi连接页到page_manager(可重复调用)
 * @note        不调用LVGL接口, 可在lv_init()之前调用
 * @param       无
 * @retval      无
 */
void wifi_page_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WIFI_PAGE_H */
