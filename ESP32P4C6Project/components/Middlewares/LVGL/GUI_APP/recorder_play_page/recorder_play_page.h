/**
 * @file recorder_play_page.h
 * @brief 录音回放页: 列出 /User/RECORDER 下的录音文件, 点击播放
 *
 * 由录音页右上角按钮进入; 与其它页面相同的 view_t + page_manager 生命周期。
 * 播放由 BSP 的 recorder 播放任务完成, 这里只负责文件列表和启动/停止。
 */

#ifndef RECORDER_PLAY_PAGE_H
#define RECORDER_PLAY_PAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORDER_PLAY_PAGE_ID   6       /* 页面ID(page_manager注册表主键) */

/**
 * @brief       注册录音回放页到page_manager(可重复调用)
 * @note        不调用LVGL接口, 可在lv_init()之前调用
 * @param       无
 * @retval      无
 */
void recorder_play_page_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RECORDER_PLAY_PAGE_H */
