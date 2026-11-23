/**
 * @file popup_base.h
 * @brief 弹窗基类: 在 view_t 之上补充弹窗特有的属性
 *
 * 与 view_t 的关系(与页面相同的C式继承):
 *   view_t       : id/name/link + 生命周期回调(on_start/on_stop/on_update...)
 *   popup_base_t : view + 弹窗属性(所属层 level / 层内优先级 priority)
 *   具体弹窗     : popup_base_t 作为第一个成员, 后面放自己的控件和状态
 *
 * 用法:
 * @code
 * typedef struct
 * {
 *     popup_base_t base;              // 必须是第一个成员
 *     lv_obj_t *panel;
 * } my_popup_t;
 *
 * static my_popup_t s_popup =
 * {
 *     .base =
 *     {
 *         .view     = { .id = 1, .name = "my", .on_start = my_on_start },
 *         .level    = POPUP_LEVEL_SYS,
 *         .priority = SYSTEM_PRIORITY0,
 *     },
 * };
 *
 * popup_systempopup_create(&s_popup.base.view, SYSTEM_PRIORITY0);
 * @endcode
 */

#ifndef POPUP_BASE_H
#define POPUP_BASE_H

#include "view.h"
#include "popup_manager.h"
#include "container_of.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 弹窗基类: 所有弹窗对象都必须以它作为第一个成员 */
typedef struct popup_base
{
    view_t view;                    /* 视图基类(必须放第一位) */
    popup_priority_t level;         /* 所属层: POPUP_LEVEL_APP / POPUP_LEVEL_SYS */
    uint32_t priority;              /* 层内优先级(数值越大越靠上) */
} popup_base_t;

/**
 * @brief       由 view_t 指针还原弹窗基类指针
 * @note        view 必须内嵌在 popup_base_t 中(即来自弹窗对象的.base.view)
 * @param       view: 弹窗的 view_t 指针
 * @retval      popup_base_t指针
 */
static inline popup_base_t *popup_base_of(view_t *view)
{
    return container_of(view, popup_base_t, view);
}

#ifdef __cplusplus
}
#endif

#endif /* POPUP_BASE_H */
