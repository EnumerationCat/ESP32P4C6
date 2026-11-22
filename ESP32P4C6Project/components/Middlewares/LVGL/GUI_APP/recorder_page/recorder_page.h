/**
 * @file recorder_page.h
 * @brief 录音页(recorder_page): 触摸操作录音, 保存到 /User/RECORDER
 *
 * 与其它页面相同的 view_t + page_manager 生命周期。
 * 只做录音: REC 开始, STOP&SAVE 停止并回填 WAV 头; 离开页面自动停止保存。
 */

#ifndef RECORDER_PAGE_H
#define RECORDER_PAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORDER_PAGE_ID    5       /* 页面ID(page_manager注册表主键) */

/**
 * @brief       注册录音页到page_manager(可重复调用)
 * @note        不调用LVGL接口, 可在lv_init()之前调用
 * @param       无
 * @retval      无
 */
void recorder_page_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RECORDER_PAGE_H */
