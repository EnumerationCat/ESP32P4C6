/**
 * @file popup_manager.c
 * @brief 弹窗管理器实现: 模仿 FreeRTOS 任务调度
 *
 * 调度模型:
 *   app / sys 两条按 priority 升序排列的 ready 链表, sys 整体优先级高于 app;
 *   同一时刻只有一个弹窗"运行"(显示中, 已调 on_start), 其余在 ready 链表排队。
 *
 *   - 抢占: 新弹窗严格高于当前运行弹窗(先比层级 sys>app, 再比 priority)时,
 *           当前弹窗被直接关闭(调 on_stop 销毁控件并出队), 高优先级退出后
 *           不会再恢复; 新弹窗立即运行;
 *   - 同优先级: 不抢占, 后来的排在已排队节点之后(FIFO), 等前面的退出后再轮到自己;
 *   - 存活时间: popup_base.value(ms), 只在运行期间倒计时;
 *           到 0 调 request_close 让弹窗自己退场, 没有回调则直接关闭; 0=常驻。
 *
 * 与 page_manager 解耦: 这里只碰弹窗对象和它们内嵌的 view_t.link,
 * 不调用任何页面的 on_pause/on_resume, 也不修改页面栈。
 */

#include "popup_manager.h"
#include "popup_base.h"
#include "container_of.h"
#include "lvgl.h"

#include <string.h>

/* 统一生命周期方法(经 g_popup_manager.create/destroy 暴露) */
static popup_base_t *popup_create(const popup_base_t *prototype, uint32_t size,
                                  popup_priority_t level, uint32_t priority);
static void popup_destroy(view_t *view);

/* 全局唯一实例(其余字段零初始化, 由 ensure_init 填充) */
popup_manager_t g_popup_manager =
{
    .create  = popup_create,
    .destroy = popup_destroy,
};

/* 节点 -> 弹窗 */
static inline view_t *popup_view_of(list_head_t *node)
{
    return container_of(node, view_t, link);
}

/* 取某一层对应的链表 */
static list_t *popup_level_list(popup_manager_t *pm, popup_priority_t level)
{
    return (level == POPUP_LEVEL_SYS) ? &pm->sys : &pm->app;
}

/* 从一条 ready 链表里挑出"接下来该运行"的弹窗:
 *   - 优先级(value)最高的那个;
 *   - 若最高优先级有多个, 取最早入队的那个(FIFO)。
 * 返回该节点对应的弹窗; 链表为空时返回 NULL。 */
static view_t *popup_list_pick(list_t *list)
{
    list_head_t *node;
    list_head_t *selected = NULL;   /* 扫描到目前为止优先级最高的节点 */

    list_for_each(node, list)
    {
        /* 只有严格更高的优先级才替换擂主:
         * 相等时保留先遇到(即更早入队)的那个, 从而同优先级满足 FIFO。 */
        if (selected == NULL || node->value > selected->value)
        {
            selected = node;
        }
    }

    return (selected != NULL) ? popup_view_of(selected) : NULL;
}

/* 全局下一个该运行的弹窗: sys 整层优先于 app */
static view_t *popup_pick_ready(popup_manager_t *pm)
{
    view_t *view = popup_list_pick(&pm->sys);

    return (view != NULL) ? view : popup_list_pick(&pm->app);
}

/* a 是否严格高于 b(先比层级, 再比 priority) */
static bool popup_strictly_higher(view_t *a, view_t *b)
{
    popup_base_t *pa = popup_base_of(a);
    popup_base_t *pb = popup_base_of(b);

    if (pa->level != pb->level)
    {
        return pa->level > pb->level;
    }

    return pa->priority > pb->priority;
}

/* 开始运行某个弹窗 */
static void popup_start_running(popup_manager_t *pm, view_t *view)
{
    pm->running  = view;
    pm->expiring = false;
    pm->run_tick = lv_tick_get();

    if (view->on_start != NULL)
    {
        view->on_start(view);
    }
}

/* 出队后释放动态实例(静态实例 dispose 为 NULL) */
static void popup_dispose(view_t *view)
{
    popup_base_t *base = popup_base_of(view);

    /* dispose==NULL 视为静态实例: 只出队不释放。
     * 动态实例: 先调类型清理钩子(从本类型实例链表摘除), 再由管理器统一释放内存。 */
    if (base->dispose != NULL)
    {
        base->dispose(view);
        lv_mem_free(base);
    }
}

/* 抢占: 被抢占的弹窗直接关闭(出队销毁, 高优先级退出后不再恢复) */
static void popup_preempt_running(popup_manager_t *pm)
{
    view_t *preempted = pm->running;

    if (preempted == NULL)
    {
        return;
    }

    pm->running  = NULL;
    pm->expiring = false;

    if (preempted->on_stop != NULL)
    {
        preempted->on_stop(preempted);
    }

    list_delete(&preempted->link);
    popup_dispose(preempted);
}

/* 调度: 选出该运行的弹窗, 必要时抢占当前运行者 */
static void popup_schedule(popup_manager_t *pm)
{
    view_t *next = popup_pick_ready(pm);

    if (next == pm->running)
    {
        return;
    }

    if (pm->running != NULL)
    {
        /* 同优先级或更低优先级不抢占, 等当前弹窗自己退出 */
        if (next == NULL || !popup_strictly_higher(next, pm->running))
        {
            return;
        }

        popup_preempt_running(pm);
    }

    if (next != NULL)
    {
        popup_start_running(pm, next);
    }
}

static void popup_manager_ensure_init(popup_manager_t *pm)
{
    if (pm->inited)
    {
        return;
    }

    list_init(&pm->app);
    list_init(&pm->sys);
    pm->running  = NULL;
    pm->expiring = false;
    pm->run_tick = 0;
    pm->inited   = true;
}

void popup_manager_init(void)
{
    popup_manager_ensure_init(&g_popup_manager);
}

/* 上架: 按优先级插入对应层的 ready 链表并尝试调度 */
static void popup_open(view_t *view, popup_priority_t level, uint32_t priority)
{
    popup_manager_t *pm = &g_popup_manager;
    popup_base_t *base;

    popup_manager_ensure_init(pm);

    if (view == NULL || view->link.owner != NULL)
    {
        return;
    }

    base = popup_base_of(view);
    base->level    = level;
    base->priority = priority;

    list_head_init(&view->link, priority);
    list_add_sorted(popup_level_list(pm, level), &view->link);

    popup_schedule(pm);                   /* 入队后尝试调度(可能抢占当前运行弹窗) */
}

/* create: 按原型分配新实例并上架(入队调度), 返回基类指针
 * 原型可以是栈上局部变量: 先填好派生数据, 再交给管理器拷贝并上架,
 * 这样 on_start 才能读到正确的内容。 */
static popup_base_t *popup_create(const popup_base_t *prototype, uint32_t size,
                                  popup_priority_t level, uint32_t priority)
{
    popup_base_t *popup;

    if (prototype == NULL || size < sizeof(popup_base_t))
    {
        return NULL;
    }

    popup = lv_mem_alloc(size);

    if (popup == NULL)
    {
        return NULL;
    }

    memcpy(popup, prototype, size);

    popup_open(&popup->view, level, priority);   /* 上架: 入队并调度 */

    return popup;
}

/* create 的系统级便捷封装 */
popup_base_t *system_popup_create(const popup_base_t *prototype, uint32_t size,
                                  systempopup_priority_t priority)
{
    return g_popup_manager.create(prototype, size, POPUP_LEVEL_SYS,
                                  (uint32_t)priority);
}

/* create 的应用级便捷封装 */
popup_base_t *app_popup_create(const popup_base_t *prototype, uint32_t size,
                               apppopup_priority_t priority)
{
    return g_popup_manager.create(prototype, size, POPUP_LEVEL_APP,
                                  (uint32_t)priority);
}

/* destroy: 停止(运行中先 on_stop 销毁控件) + 出队 + 释放动态实例内存 */
static void popup_destroy(view_t *view)
{
    popup_manager_t *pm = &g_popup_manager;

    popup_manager_ensure_init(pm);

    if (view == NULL || view->link.owner == NULL)
    {
        return;
    }

    if (view->link.owner != &pm->app && view->link.owner != &pm->sys)
    {
        return;
    }

    if (view == pm->running)
    {
        pm->running  = NULL;
        pm->expiring = false;

        if (view->on_stop != NULL)
        {
            view->on_stop(view);
        }

        list_delete(&view->link);
        popup_dispose(view);
        popup_schedule(pm);               /* 让排队的下一个弹窗接管 */
        return;
    }

    /* 排队中(未运行)的弹窗: 控件还没创建, 直接出队释放 */
    list_delete(&view->link);
    popup_dispose(view);
}

void popup_manager_close_all(popup_priority_t level)
{
    popup_manager_t *pm = &g_popup_manager;
    list_t *list;
    list_head_t *node;

    popup_manager_ensure_init(pm);

    list = popup_level_list(pm, level);

    while ((node = list_first(list)) != NULL)
    {
        view_t *view = popup_view_of(node);

        if (view == pm->running)
        {
            pm->running  = NULL;
            pm->expiring = false;

            if (view->on_stop != NULL)
            {
                view->on_stop(view);
            }
        }

        list_delete(node);
        popup_dispose(view);
    }

    popup_schedule(pm);
}

view_t *popup_manager_top(popup_priority_t level)
{
    popup_manager_ensure_init(&g_popup_manager);

    return popup_list_pick(popup_level_list(&g_popup_manager, level));
}

view_t *popup_manager_back(void)
{
    view_t *top = popup_manager_top(POPUP_LEVEL_SYS);

    if (top == NULL)
    {
        top = popup_manager_top(POPUP_LEVEL_APP);
    }

    if (top != NULL)
    {
        popup_destroy(top);
    }

    return top;
}

bool popup_manager_is_open(view_t *view)
{
    list_t *owner;

    if (view == NULL || view->link.owner == NULL)
    {
        return false;
    }

    owner = view->link.owner;

    return (owner == &g_popup_manager.app || owner == &g_popup_manager.sys);
}

void popup_manager_tick(void)
{
    popup_manager_t *pm = &g_popup_manager;
    list_head_t *node;

    popup_manager_ensure_init(pm);

    /* 存活时间到点: 让弹窗自己退场(有回调), 否则直接关闭 */
    if (pm->running != NULL && !pm->expiring)
    {
        popup_base_t *base = popup_base_of(pm->running);

        if (base->value > 0 && lv_tick_elaps(pm->run_tick) >= base->value)
        {
            if (base->request_close != NULL)
            {
                if (base->request_close(pm->running))
                {
                    pm->expiring = true;
                }
                else
                {
                    pm->run_tick = lv_tick_get();   /* 这次没退出, 重新计时 */
                }
            }
            else
            {
                popup_destroy(pm->running);
            }
        }
    }

    list_for_each(node, &pm->app)
    {
        view_t *view = popup_view_of(node);

        if (view->on_update != NULL)
        {
            view->on_update(view);
        }
    }

    list_for_each(node, &pm->sys)
    {
        view_t *view = popup_view_of(node);

        if (view->on_update != NULL)
        {
            view->on_update(view);
        }
    }
}
