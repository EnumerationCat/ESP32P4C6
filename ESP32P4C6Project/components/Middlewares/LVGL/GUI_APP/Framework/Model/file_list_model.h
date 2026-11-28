/**
 * @file file_list_model.h
 * @brief 通用文件列表数据层(model): 按目录+扩展名扫描并缓存路径
 *
 * 分层约定:
 *  - 本层不依赖LVGL, 不调用任何 lv_* 接口;
 *  - 列表实例由调用方(页面)持有, 所以不同页面互不影响;
 *  - 只在 LVGL 任务里调用(页面创建/恢复时), 无锁。
 *
 * 用法:
 *   file_list_t files;                                  // 页面 ui 结构里的成员
 *   file_list_scan(&files, "/User/VIDEO", "avi");
 *   for (i = 0; i < file_list_count(&files); i++)
 *       ... file_list_name(&files, i) / file_list_path(&files, i) ...
 */

#ifndef FILE_LIST_MODEL_H
#define FILE_LIST_MODEL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILE_LIST_MAX           32      /* 单个列表最多缓存的文件数 */
#define FILE_LIST_PATH_LEN      300     /* 完整路径长度上限 */

typedef struct
{
    int count;                                          /* 已缓存文件数 */
    char paths[FILE_LIST_MAX][FILE_LIST_PATH_LEN];      /* 完整路径 */
} file_list_t;

/**
 * @brief       扫描目录并重建列表(幂等; 目录打不开时列表为空)
 * @param       list: 调用方持有的列表实例
 * @param       dir : 目录(如 "/User/VIDEO")
 * @param       ext : 扩展名(如 "avi" 或 ".avi", 不区分大小写)
 * @retval      无
 */
void file_list_scan(file_list_t *list, const char *dir, const char *ext);

/**
 * @brief       当前缓存的文件数
 * @param       list: 列表实例
 * @retval      文件数(列表为NULL时返回0)
 */
int file_list_count(const file_list_t *list);

/**
 * @brief       第 index 个文件的文件名(不含路径; 指向内部缓冲, 不要释放)
 * @param       list : 列表实例
 * @param       index: [0, count)
 * @retval      文件名; 越界返回NULL
 */
const char *file_list_name(const file_list_t *list, int index);

/**
 * @brief       第 index 个文件的完整路径(指向内部缓冲, 不要释放)
 * @param       list : 列表实例
 * @param       index: [0, count)
 * @retval      路径; 越界返回NULL
 */
const char *file_list_path(const file_list_t *list, int index);

#ifdef __cplusplus
}
#endif

#endif /* FILE_LIST_MODEL_H */
