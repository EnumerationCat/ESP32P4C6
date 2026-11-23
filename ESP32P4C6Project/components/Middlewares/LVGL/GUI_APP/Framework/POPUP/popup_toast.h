/**
 * @file popup_toast.h
 * @brief 屏幕中心淡入提示弹窗(Framework/POPUP)
 *
 * view_t派生对象, 由 popup_manager 以系统级(POPUP_LEVEL_SYS)管理:
 *  - 控件挂在 lv_layer_sys(), 永远盖在应用级弹窗和页面之上,
 *    应用级弹窗不能打断/盖住它;
 *  - 跨页面保留, 切换页面时不会被清掉;
 *  - 打开/关闭不会暂停页面, 也不改变页面栈;
 *  - 不可点击, 不会挡住底层的触摸;
 *  - 只能在 LVGL 任务中调用。
 *
 * 与 popup_notice 的区别: notice 从顶部滑入, toast 在屏幕正中央淡入淡出。
 *
 * 用法示例:
 * @code
 * popup_toast_show("Saved", 1500);       // 1.5 秒后自动收回
 * popup_toast_show("Please wait...", 0); // 常驻, 需手动 popup_toast_hide()
 * @endcode
 */

#ifndef POPUP_TOAST_H
#define POPUP_TOAST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       显示屏幕中心提示弹窗(淡入, 文字居中)
 * @note        正在显示时只更新文字并重新计时; 从任务/中断里调用需自行投递到LVGL任务
 * @param       text:        提示文字(不能为NULL)
 * @param       duration_ms: 自动收回时间; 0表示常驻, 需手动 popup_toast_hide()
 * @retval      无
 */
void popup_toast_show(const char *text, uint32_t duration_ms);

/**
 * @brief       手动收回中心提示弹窗(淡出后关闭, 未显示时忽略)
 * @param       无
 * @retval      无
 */
void popup_toast_hide(void);

/**
 * @brief       中心提示弹窗是否正在显示(含淡出过程)
 * @param       无
 * @retval      是/否
 */
bool popup_toast_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* POPUP_TOAST_H */
