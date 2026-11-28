/**
 * @file view.h
 * @brief 视图基类(Framework): 统一的生命周期/事件接口
 *
 * 用C实现"继承"的方式与Linux内核一致:
 *  - 派生视图把 view_t 作为自己结构体的第一个成员;
 *  - 框架只持有 view_t *(基类指针), 回调时传 self;
 *  - 派生侧用 container_of(self, 派生类型, 基类成员名) 还原出自己的结构体。
 *
 * 这样所有视图都能被同一套代码初始化/切换/刷新, 派生视图互不感知。
 *
 * 使用示例:
 * @code
 * typedef struct { view_t base; lv_obj_t *screen; } my_view_t;
 * static my_view_t s_my =
 * {
 *     .base = { .id = MY_PAGE_ID, .name = "my", .init = my_init, .on_update = my_update, },
 * };
 * static void my_init(view_t *self)
 * {
 *     my_view_t *v = container_of(self, my_view_t, base);
 *     ...
 * }
 * @endcode
 */

#ifndef VIEW_H
#define VIEW_H

#include <stdint.h>

#include "list_head.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct view view_t;

struct view
{
    uint16_t id;                                          /* 页面ID(注册表主键, 0为无效) */
    const char *name;                                     /* 页面名字(日志/调试用), 建议用字符串常量 */
    list_head_t link;                                     /* 侵入式状态节点(由page_manager维护) */

    void (*init)(view_t *self);                           /* 创建资源/控件 */
    void (*deinit)(view_t *self);                         /* 释放资源/控件 */
    void (*on_start)(view_t *self);                       /* 进入视图(获得焦点) */
    void (*on_pause)(view_t *self);                       /* 离开视图(失去焦点, 资源保留) */
    void (*on_resume)(view_t *self);                      /* 从挂起恢复(重新获得焦点, 资源仍在) */
    void (*on_update)(view_t *self);                      /* 周期刷新 */
    void (*on_stop)(view_t *self);                        /* 停止视图 */
    /* 控件点击: click_id为控件标识字符串, user_data为附加参数(列表行号等) */
    void (*on_click)(view_t *self, const char *click_id, intptr_t user_data);
    /* 异步事件: 由 Service/后端经 app_event 投递的字符串ID, user_data为附带参数(按值) */
    void (*on_event)(view_t *self, const char *event_id, intptr_t user_data);
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*VIEW_H*/
