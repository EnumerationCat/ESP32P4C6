#ifndef __CAMERA_PAGE_H__
#define __CAMERA_PAGE_H__

/**
 * @file camera_page.h
 * @brief 相机页: 摄像头画面经 PPA 缩放后由 lv_img 显示, 支持叠加返回按钮
 */

#define CAMERA_PAGE_ID   8       /* 页面ID(page_manager注册表主键) */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       注册相机页(在 app_main 的 pages_init_all() 里调用)
 * @param       无
 * @retval      无
 */
void camera_page_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAMERA_PAGE_H__ */
