/**
 * @file popup_notice.h
 * @brief 顶部滑入提示弹窗(Framework/POPUP)
 *
 * view_t派生对象, 由 popup_manager 以系统级(POPUP_LEVEL_SYS)管理:
 *  - 每次 show 新建一个独立实例, 多实例按优先级排队/抢占;
 *  - 系统级优先级高于应用级, 弹出时会抢占并关闭正在显示的应用级弹窗;
 *  - 存活时间由调度器管理: 到时自动收回, duration_ms=0 表示常驻;
 *  - 跨页面保留, 切换页面时不会被清掉;
 *  - 打开/关闭不会暂停页面, 也不改变页面栈;
 *  - 只能在 LVGL 任务中调用。
 *
 * 用法示例:
 * @code
 * popup_notice_show("Saved", 2000);       // 2 秒后自动收回
 * popup_notice_show("Connecting...", 0);  // 常驻, 需手动 popup_notice_hide()
 * @endcode
 */

#ifndef POPUP_NOTICE_H
#define POPUP_NOTICE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       显示顶部提示弹窗(从屏幕上方滑入, 文字居中)
 * @note        每次调用都新建一个独立实例, 由 popup_manager 排队/抢占;
 *              从任务/中断里调用需自行投递到LVGL任务
 * @param       text:        提示文字(不能为NULL)
 * @param       duration_ms: 自动收回时间; 0表示常驻, 需手动 popup_notice_hide()
 * @retval      无
 */
void popup_notice_show(const char *text, uint32_t duration_ms);

/**
 * @brief       收回所有顶部提示弹窗(运行中的滑出关闭, 排队中的直接出队)
 * @param       无
 * @retval      无
 */
void popup_notice_hide(void);

/**
 * @brief       是否有提示弹窗正在显示(含滑出过程)
 * @param       无
 * @retval      是/否
 */
bool popup_notice_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* POPUP_NOTICE_H */
