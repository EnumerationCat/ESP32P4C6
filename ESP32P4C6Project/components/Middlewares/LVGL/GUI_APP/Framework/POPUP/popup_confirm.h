/**
 * @file popup_confirm.h
 * @brief 底部双按钮确认弹窗(Framework/POPUP)
 *
 * view_t派生对象, 由 popup_manager 以应用级(POPUP_LEVEL_APP)管理:
 *  - 控件挂在 lv_layer_top(), 会被系统级弹窗(如提示弹窗)盖住,
 *    不能打断系统级弹窗;
 *  - 跟随页面, 切换页面时自动关闭;
 *  - 打开/关闭不会暂停页面, 也不改变页面栈;
 *  - 只能在 LVGL 任务中调用。
 *
 * 用法示例:
 * @code
 * popup_confirm_show("Delete this file?", "Cancel", "OK",
 *                    on_cancel, on_ok, NULL);
 * @endcode
 */

#ifndef POPUP_CONFIRM_H
#define POPUP_CONFIRM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 双按钮弹窗的按钮回调 */
typedef void (*popup_action_cb_t)(void *user_data);

/**
 * @brief       显示底部双按钮确认弹窗(从屏幕下方滑入)
 * @note        描述文本居中, 两个按钮在底部并排; 回调在弹窗关闭前调用,
 *              回调里可以安全地切页(切页会自动收掉弹窗)
 * @param       desc:       描述文本(不能为NULL)
 * @param       left_text:  左按钮文字(如 "Cancel")
 * @param       right_text: 右按钮文字(如 "OK")
 * @param       left_cb:    左按钮回调, 可为NULL
 * @param       right_cb:   右按钮回调, 可为NULL
 * @param       user_data:  回调参数
 * @retval      无
 */
void popup_confirm_show(const char *desc,
                        const char *left_text,
                        const char *right_text,
                        popup_action_cb_t left_cb,
                        popup_action_cb_t right_cb,
                        void *user_data);

/**
 * @brief       手动关闭确认弹窗(滑出后关闭, 未显示时忽略)
 * @param       无
 * @retval      无
 */
void popup_confirm_hide(void);

/**
 * @brief       确认弹窗是否正在显示(含滑出过程)
 * @param       无
 * @retval      是/否
 */
bool popup_confirm_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* POPUP_CONFIRM_H */
