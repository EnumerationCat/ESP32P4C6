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
 * g_popup_manager.create(&s_popup.base, sizeof(s_popup),
 *                        POPUP_LEVEL_SYS, SYSTEM_PRIORITY0);
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
    uint32_t priority;              /* 层内优先级(数值越大优先级越高) */

    /* 存活时间(ms), 模仿任务的时间片: 只在"运行"(显示中)时消耗,
     * 被高优先级弹窗抢占时弹窗直接关闭, 不再倒计时;
     * 0 表示常驻(不自动退出)。到 0 由 popup_manager 调 request_close 退出。 */
    uint32_t value;

    /* 到期退出回调: popup_manager 到期时调用, 让弹窗自己播退场动画,
     * 动画结束再调 g_popup_manager.destroy(); 返回 true 表示已开始退场,
     * 返回 false 表示本次没有退出(如正在淡入), 调度器会重新计时。
     * 为 NULL 时由调度器直接 g_popup_manager.destroy()。 */
    bool (*request_close)(view_t *view);

    /* 类型清理钩子: 弹窗从管理器出队时调用, 实例在此做类型级清理
     * (如从本类型实例链表摘除); 内存由管理器销毁流程统一 lv_mem_free,
     * 这里不要自行释放。静态实例保持 NULL(只出队不释放)。 */
    void (*request_delete)(view_t *view);
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
