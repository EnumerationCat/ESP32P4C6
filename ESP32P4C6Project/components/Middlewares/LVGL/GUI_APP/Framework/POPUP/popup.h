/**
 * @file popup.h
 * @brief 弹窗(Framework/POPUP): 顶部滑入提示 + 底部双按钮确认
 *
 * 两个弹窗都是 view_t(Framework/View/view.h) 派生对象, 走 page_manager 的
 * 弹窗接口 page_popup_open()/page_popup_close():
 *  - 打开时底层页 on_pause, 全部弹窗关闭后底层页 on_resume;
 *  - 控件挂到 lv_layer_top() 上, 覆盖在当前页面之上, 不替换页面屏幕;
 *  - 只能在 LVGL 任务中调用。
 *
 * 用法示例:
 * @code
 * popup_notice_show("Saved", 2000);                     // 2 秒后自动收回
 * popup_confirm_show("Delete this file?", "Cancel", "OK",
 *                    on_cancel, on_ok, NULL);
 * @endcode
 */

#ifndef POPUP_H
#define POPUP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 弹窗优先级(数值越大越靠上) */
#define POPUP_PRIORITY_NOTICE    10
#define POPUP_PRIORITY_CONFIRM   20

/* 双按钮弹窗的按钮回调 */
typedef void (*popup_action_cb_t)(void *user_data);

/**
 * @brief       显示顶部提示弹窗(从屏幕上方滑入, 文字居中)
 * @note        正在显示时只更新文字并重新计时; 从任务/中断里调用需自行投递到LVGL任务
 * @param       text:        提示文字(不能为NULL)
 * @param       duration_ms: 自动收回时间; 0表示常驻, 需手动 popup_notice_hide()
 * @retval      无
 */
void popup_notice_show(const char *text, uint32_t duration_ms);

/**
 * @brief       手动收回顶部提示弹窗(滑出后关闭, 未显示时忽略)
 * @param       无
 * @retval      无
 */
void popup_notice_hide(void);

/**
 * @brief       顶部提示弹窗是否正在显示(含滑出过程)
 * @param       无
 * @retval      是/否
 */
bool popup_notice_visible(void);

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

#endif /* POPUP_H */
