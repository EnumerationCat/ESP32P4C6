/**
 * @file video_page.h
 * @brief 视频页(video_page): 列出 SD 卡 /User/VIDEO 下的 AVI 文件, 点击播放
 *
 * 页面本身是 view_t, 与 user_home_page/shell_page 使用同一套 page_manager 生命周期。
 * 播放期间画面由 videoplay 任务独占, LVGL 由 main 的 lvgl_task 暂停刷新;
 * 播放结束后自动回到本页面。
 */

#ifndef VIDEO_PAGE_H
#define VIDEO_PAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_PAGE_ID   3       /* 页面ID(page_manager注册表主键) */

/**
 * @brief       注册视频页到page_manager(可重复调用)
 * @note        不调用LVGL接口, 可在lv_init()之前调用
 * @param       无
 * @retval      无
 */
void video_page_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VIDEO_PAGE_H */
