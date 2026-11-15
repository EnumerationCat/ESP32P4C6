/**
 * @file user_home_page.h
 * @brief 主界面页面(user_home_page): 相机预览入口 + 命令行入口
 *
 * 页面本身是 view_t(Framework/View/view.h), 注册到 page_manager 后按ID切换:
 *   manager_gopage(USER_HOME_PAGE_ID)  进入/恢复到主界面
 *   on_start  首次进入时创建控件并载入屏幕
 *   on_resume 从挂起恢复时直接重新载入屏幕(资源保留)
 */

#ifndef USER_HOME_PAGE_H
#define USER_HOME_PAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *      DEFINES
 **********************/
#define USER_HOME_PAGE_ID   1       /* 页面ID(page_manager注册表主键) */

/**********************
 *      TYPEDEFS
 **********************/
typedef void (*user_home_page_camera_cb_t)(void);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief       注册主界面页面到page_manager(可重复调用)
 * @note        不调用LVGL接口, 可在lv_init()之前调用
 * @param       无
 * @retval      无
 */
void user_home_page_init(void);

/**
 * @brief       注册相机按钮回调
 * @param       cb: 回调函数
 * @retval      无
 */
void user_home_page_set_camera_cb(user_home_page_camera_cb_t cb);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*USER_HOME_PAGE_H*/
