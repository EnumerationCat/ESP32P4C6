/**
 * @file popup_manager.h
 * @brief 弹窗管理器(Framework/Manage): 应用级/系统级两层优先队列
 *
 * 与 page_manager 的分工:
 *   page_manager  : 只管理页面(ready/paused), 负责页面切换与挂起恢复;
 *   popup_manager : 只管理弹窗, 打开/关闭弹窗不会暂停页面, 也不改变页面栈。
 *
 * 两层弹窗:
 *   POPUP_LEVEL_APP: 应用级, 随页面切换由 manager_gopage() 清空;
 *                    控件绘制在 lv_layer_top(), 覆盖当前页面。
 *   POPUP_LEVEL_SYS: 系统级, 跨页面保留, 可打断应用级弹窗;
 *                    控件绘制在 lv_layer_sys(), 永远在最上层
 *                    (LVGL 输入也是先查 sys 层, 再查 top 层, 最后查屏幕)。
 *
 * 两个队列都按 priority 升序排列, 表尾优先级最高。
 * 弹窗对象必须是长期存活的 view_t(静态/全局), 管理器只串指针, 不负责内存。
 * 所有接口只能在 LVGL 任务中调用。
 */

#ifndef POPUP_MANAGER_H
#define POPUP_MANAGER_H

#include "view.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    POPUP_LEVEL_APP = 0,                /* 应用级: 跟随页面 */
    POPUP_LEVEL_SYS = 1,                /* 系统级: 跨页面, 永远在上 */
} popup_priority_t;

typedef enum
{
    APP_PRIORITY0 = 0,                  /* 应用级队列内优先级 */
    APP_PRIORITY1 = 1,
} apppopup_priority_t;

typedef enum
{
    SYSTEM_PRIORITY0 = 0,               /* 系统级队列内优先级 */
    SYSTEM_PRIORITY1 = 1,
} systempopup_priority_t;

/* 弹窗基类(定义见 POPUP/popup_base.h, 这里只前向声明指针) */
struct popup_base;






/**
 * @brief       初始化弹窗管理器(可重复调用)
 * @param       无
 * @retval      无
 */
void popup_manager_init(void);

/**
 * @brief       打开弹窗: 调 on_start 后按优先级插入对应层
 * @note        已在任一队列上的对象会被忽略
 * @param       view:     弹窗对象
 * @param       level:    所属层(应用级/系统级)
 * @param       priority: 同层内优先级(数值越大越靠上)
 * @retval      无
 */
void popup_manager_open(view_t *view, popup_priority_t level, uint32_t priority);

/**
 * @brief       创建(打开)一个弹窗, 层级/优先级显式指定
 * @note        view 必须内嵌在 popup_base_t 中(即弹窗从 popup_base_t 派生),
 *              本函数会通过 container_of 还原 popup_base_t 并记录 level/priority
 * @param       view:     弹窗的 view_t 指针(&popup->base.view)
 * @param       level:    所属层(应用级/系统级)
 * @param       priority: 层内优先级(数值越大越靠上)
 * @retval      popup_base_t指针; view为NULL时返回NULL
 */
struct popup_base *popup_create(view_t *view, popup_priority_t level,
                                uint32_t priority);

/**
 * @brief       创建(打开)一个系统级弹窗
 * @note        等价于 popup_create(view, POPUP_LEVEL_SYS, priority);
 *              跨页面保留, 控件应绘制在 lv_layer_sys()
 * @param       view:     弹窗的 view_t 指针(&popup->base.view)
 * @param       priority: 系统级队列内优先级(数值越大越靠上)
 * @retval      popup_base_t指针; view为NULL时返回NULL
 */
struct popup_base *popup_systempopup_create(view_t *view,
                                            systempopup_priority_t priority);

/**
 * @brief       创建(打开)一个应用级弹窗
 * @note        等价于 popup_create(view, POPUP_LEVEL_APP, priority);
 *              随页面切换关闭, 控件应绘制在 lv_layer_top()
 * @param       view:     弹窗的 view_t 指针(&popup->base.view)
 * @param       priority: 应用级队列内优先级(数值越大越靠上)
 * @retval      popup_base_t指针; view为NULL时返回NULL
 */
struct popup_base *popup_apppopup_create(view_t *view,
                                         apppopup_priority_t priority);

/**
 * @brief       关闭指定弹窗: 调 on_stop 后从队列移除
 * @param       view: 弹窗对象(不在队列中则忽略)
 * @retval      无
 */
void popup_manager_close(view_t *view);

/**
 * @brief       关闭某一层的所有弹窗(从优先级最高的开始)
 * @param       level: 弹窗层
 * @retval      无
 */
void popup_manager_close_all(popup_priority_t level);

/**
 * @brief       取某一层当前优先级最高的弹窗
 * @param       level: 弹窗层
 * @retval      弹窗对象; 无弹窗返回NULL
 */
view_t *popup_manager_top(popup_priority_t level);

/**
 * @brief       关闭最上层的一个弹窗(page_back 用)
 * @note        先关系统级, 系统级没有时再关应用级
 * @param       无
 * @retval      被关闭的弹窗; 没有弹窗返回NULL
 */
view_t *popup_manager_back(void);

/**
 * @brief       弹窗是否正在显示(在任一队列中)
 * @param       view: 弹窗对象
 * @retval      是/否
 */
bool popup_manager_is_open(view_t *view);

/**
 * @brief       周期刷新: 依次调用所有弹窗的 on_update
 * @note        压在下面的弹窗也会刷新; on_update 里不要关闭弹窗
 * @param       无
 * @retval      无
 */
void popup_manager_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* POPUP_MANAGER_H */
