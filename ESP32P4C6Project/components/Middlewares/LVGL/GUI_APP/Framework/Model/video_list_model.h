/**
 * @file video_list_model.h
 * @brief 视频文件列表数据层(model): 扫描 /User/VIDEO 下的 .avi 并缓存路径
 *
 * 分层约定:
 *  - 本层不依赖LVGL, 不调用任何 lv_* 接口;
 *  - 只在 LVGL 任务里调用(页面创建/刷新列表时), 无锁;
 *  - 显示层(view)只通过 count/name/path 取数据, 自己不再持有扫描逻辑。
 */

#ifndef VIDEO_LIST_MODEL_H
#define VIDEO_LIST_MODEL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_LIST_MAX          24      /* 列表最多缓存的文件数 */
#define VIDEO_LIST_PATH_LEN     300     /* 完整路径长度上限 */

/**
 * @brief       扫描视频目录并重建缓存(幂等)
 * @param       无
 * @retval      无
 */
void video_list_model_refresh(void);

/**
 * @brief       当前缓存的文件数
 * @param       无
 * @retval      文件数(0 表示目录打不开或没有 .avi)
 */
int video_list_model_count(void);

/**
 * @brief       第 index 个文件的文件名(不含路径)
 * @param       index: [0, count)
 * @retval      文件名; 越界返回NULL
 */
const char *video_list_model_name(int index);

/**
 * @brief       第 index 个文件的完整路径
 * @param       index: [0, count)
 * @retval      路径; 越界返回NULL
 */
const char *video_list_model_path(int index);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_LIST_MODEL_H */
