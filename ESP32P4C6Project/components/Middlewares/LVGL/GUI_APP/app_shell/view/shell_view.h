/**
 * @file shell_view.h
 * @brief 命令行显示层(view): LCD文本页、终端文本标签、输入光标
 *
 * 分层约定:
 *  - 本层可以依赖 model(shell_core)和LVGL;
 *  - 所有接口都必须在LVGL任务上下文中调用。
 */

#ifndef SHELL_VIEW_H
#define SHELL_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      DEFINES
 *********************/
#define SHELL_BAR_H            36      /* LCD顶栏高度 */
#define SHELL_CURSOR_BLINK_MS  500     /* 光标闪烁周期(ms) */

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief       周期刷新LCD文本页(只在LVGL任务中调用)
 * @note        与 lv_timer_handler() 同一任务中周期调用
 * @param       无
 * @retval      无
 */
void shell_view_tick(void);

/**
 * @brief       切换到LCD文本页(只在LVGL任务中调用)
 * @note        进入时把终端文本重置为一行提示符, 并请求串口补打提示符
 * @param       无
 * @retval      无
 */
void shell_view_open(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*SHELL_VIEW_H*/
