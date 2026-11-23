/**
 * @file popup_toast.h
 * @brief 屏幕中心淡入提示弹窗(Framework/POPUP)
 *
 * view_t派生对象, 由 popup_manager 以系统级(POPUP_LEVEL_SYS)管理:
 *  - 每次 show 新建一个独立实例, 多实例按优先级排队/抢占;
 *  - 系统级优先级高于应用级, 弹出时会关闭正在显示的应用级弹窗;
 *  - 存活时间由调度器管理: 到时自动收回, duration_ms=0 表示常驻;
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
 * @note        每次调用都新建一个独立实例, 由 popup_manager 排队/抢占;
 *              从任务/中断里调用需自行投递到LVGL任务
 * @param       text:        提示文字(不能为NULL)
 * @param       duration_ms: 自动收回时间; 0表示常驻, 需手动 popup_toast_hide()
 * @retval      无
 */
void popup_toast_show(const char *text, uint32_t duration_ms);

/**
 * @brief       收回所有中心提示弹窗(运行中的淡出关闭, 排队中的直接出队)
 * @param       无
 * @retval      无
 */
void popup_toast_hide(void);

/**
 * @brief       是否有中心提示弹窗正在显示(含淡出过程)
 * @param       无
 * @retval      是/否
 */
bool popup_toast_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* POPUP_TOAST_H */
