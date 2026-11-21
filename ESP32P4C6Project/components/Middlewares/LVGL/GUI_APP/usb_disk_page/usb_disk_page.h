/**
 * @file usb_disk_page.h
 * @brief U盘模式页: 把SD卡通过USB口暴露给PC读写
 *
 * 与其它页面相同的 view_t + page_manager 生命周期。
 * 进入U盘模式后USB鼠标不可用, 退出后自动恢复。
 */

#ifndef USB_DISK_PAGE_H
#define USB_DISK_PAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_DISK_PAGE_ID    4       /* 页面ID(page_manager注册表主键) */

/**
 * @brief       注册U盘模式页到page_manager(可重复调用)
 * @note        不调用LVGL接口, 可在lv_init()之前调用
 * @param       无
 * @retval      无
 */
void usb_disk_page_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* USB_DISK_PAGE_H */
