/**
 * @file popup_manager.c
 * @brief 弹窗管理器实现: 模仿 FreeRTOS 任务调度
 *
 * 调度模型:
 *   app / sys 两条按 priority 升序排列的 ready 链表, sys 整体优先级高于 app,同优先级FIFO;
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

/**
 * @brief       统一生命周期方法(经 g_popup_manager.create/destroy 暴露)
 */
static popup_base_t *popup_create(const popup_base_t *prototype, uint32_t size,
                                  popup_priority_t level, uint32_t priority);
static void popup_destroy(view_t *view);

/* 全局唯一实例(其余字段零初始化, 由 ensure_init 填充) */
popup_manager_t g_popup_manager =
{
    .create  = popup_create,
    .destroy = popup_destroy,
};

/**
 * @brief       链表节点转换为所属弹窗视图
 * @param       node: 侵入式链表节点(view_t.link)
 * @retval      节点所属的弹窗视图
 */
static inline view_t *popup_view_of(list_head_t *node)
{
    return container_of(node, view_t, link);
}

/**
 * @brief       取指定层级对应的 ready 链表
 * @param       pm:    弹窗管理器实例
 * @param       level: 弹窗层级(POPUP_LEVEL_APP / POPUP_LEVEL_SYS)
 * @retval      该层级的 ready 链表
 */
static list_t *popup_level_list(popup_manager_t *pm, popup_priority_t level)
{
    return (level == POPUP_LEVEL_SYS) ? &pm->sys : &pm->app;
}

/**
 * @brief       从一条 ready 链表中选出下一个该运行的弹窗
 * @note        优先级(link.value)最高者优先; 优先级相同则取最早入队者(FIFO)
 * @param       list: 待扫描的 ready 链表
 * @retval      选中的弹窗视图; 链表为空返回 NULL
 */
static view_t *popup_list_pick(list_t *list)
{
    list_head_t *node;
    list_head_t *nextnode = NULL;   /* 扫描到目前为止优先级最高的节点 */

    list_for_each(node, list)
    {
        /* 只有严格更高的优先级才替换擂主:
         * 相等时保留先遇到(即更早入队)的那个, 从而同优先级满足 FIFO。 */
        if (nextnode == NULL || node->value > nextnode->value)
        {
            nextnode = node;
        }
    }

    return (nextnode != NULL) ? popup_view_of(nextnode) : NULL;
}

/**
 * @brief       选出全局下一个该运行的弹窗
 * @note        sys 整层优先于 app
 * @param       pm: 弹窗管理器实例
 * @retval      选中的弹窗视图; 两层都为空返回 NULL
 */
static view_t *popup_pick_ready(popup_manager_t *pm)
{
    view_t *view = popup_list_pick(&pm->sys);

    return (view != NULL) ? view : popup_list_pick(&pm->app);
}

/**
 * @brief       判断弹窗 a 是否严格高于弹窗 b
 * @note        先比较层级(sys > app), 层级相同再比较 priority
 * @param       a: 待比较弹窗
 * @param       b: 基准弹窗
 * @retval      a 严格高于 b 返回 true, 否则 false
 */
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

/**
 * @brief       让指定弹窗开始运行(记录运行时间戳并调用 on_start)
 * @param       pm:   弹窗管理器实例
 * @param       view: 待启动的弹窗视图
 * @retval      无
 */
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

/**
 * @brief       出队后释放弹窗实例
 * @note        request_delete==NULL 视为静态实例, 只出队不释放; 动态实例先调用类型
 *              清理钩子(从本类型实例链表摘除), 再由管理器统一 lv_mem_free
 * @param       view: 待释放的弹窗视图
 * @retval      无
 */
static void popup_delete(view_t *view)
{
    popup_base_t *base = popup_base_of(view);

    /* 静态实例 request_delete==NULL, 只出队不释放; 动态实例走类型清理后统一释放 */
    if (base->request_delete != NULL)
    {
        base->request_delete(view);
        lv_mem_free(base);
    }
}

/**
 * @brief       抢占并关闭当前运行中的弹窗
 * @note        被抢占的弹窗直接出队销毁, 高优先级退出后不再恢复
 * @param       pm: 弹窗管理器实例
 * @retval      无
 */
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
    popup_delete(preempted);
}

/**
 * @brief       调度: 选出该运行的弹窗, 必要时抢占当前运行者
 * @param       pm: 弹窗管理器实例
 * @retval      无
 */
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

/**
 * @brief       懒初始化: 首次调用时初始化两条 ready 链表
 * @param       pm: 弹窗管理器实例
 * @retval      无
 */
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

/**
 * @brief       初始化弹窗管理器(可重复调用)
 * @param       无
 * @retval      无
 */
void popup_manager_init(void)
{
    popup_manager_ensure_init(&g_popup_manager);
}

/**
 * @brief       上架弹窗: 记录层级/优先级, 插入对应 ready 链表并尝试调度
 * @param       view:     待上架的弹窗视图
 * @param       level:    弹窗层级
 * @param       priority: 层内优先级(数值越大越优先)
 * @retval      无
 */
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

/**
 * @brief       按原型分配新实例并上架(入队调度)
 * @note        原型可以是栈上局部变量: 先填好派生数据, 再交给管理器拷贝并上架,
 *              这样 on_start 才能读到正确的内容; 返回值首成员为 popup_base_t
 * @param       prototype: 实例模板(整块派生结构的原型)
 * @param       size:      实例总大小(字节, 至少为 sizeof(popup_base_t))
 * @param       level:     弹窗层级
 * @param       priority:  层内优先级(数值越大越优先)
 * @retval      新的弹窗基类指针; 参数非法或分配失败返回 NULL
 */
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

/**
 * @brief       按原型创建并上架一个系统级弹窗(create 的便捷封装)
 * @note        等价于 g_popup_manager.create(prototype, size, POPUP_LEVEL_SYS, priority);
 *              跨页面保留, 控件应绘制在 lv_layer_sys()
 * @param       prototype: 实例模板(整块派生结构的原型)
 * @param       size:      实例总大小(字节, 至少为 sizeof(popup_base_t))
 * @param       priority:  系统级队列内优先级(数值越大越靠上)
 * @retval      新的弹窗基类指针; 分配失败返回 NULL
 */
popup_base_t *system_popup_create(const popup_base_t *prototype, uint32_t size,
                                  systempopup_priority_t priority)
{
    return g_popup_manager.create(prototype, size, POPUP_LEVEL_SYS,
                                  (uint32_t)priority);
}

/**
 * @brief       按原型创建并上架一个应用级弹窗(create 的便捷封装)
 * @note        等价于 g_popup_manager.create(prototype, size, POPUP_LEVEL_APP, priority);
 *              随页面切换关闭, 控件应绘制在 lv_layer_top()
 * @param       prototype: 实例模板(整块派生结构的原型)
 * @param       size:      实例总大小(字节, 至少为 sizeof(popup_base_t))
 * @param       priority:  应用级队列内优先级(数值越大越靠上)
 * @retval      新的弹窗基类指针; 分配失败返回 NULL
 */
popup_base_t *app_popup_create(const popup_base_t *prototype, uint32_t size,
                               apppopup_priority_t priority)
{
    return g_popup_manager.create(prototype, size, POPUP_LEVEL_APP,
                                  (uint32_t)priority);
}

/**
 * @brief       销毁弹窗: 停止(运行中先调 on_stop) + 出队 + 释放动态实例内存
 * @param       view: 待销毁的弹窗视图
 * @retval      无
 */
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
        popup_delete(view);
        popup_schedule(pm);               /* 让排队的下一个弹窗接管 */
        return;
    }

    /* 排队中(未运行)的弹窗: 控件还没创建, 直接出队释放 */
    list_delete(&view->link);
    popup_delete(view);
}

/**
 * @brief       关闭某一层的所有弹窗(从优先级最高的开始)
 * @param       level: 弹窗层级
 * @retval      无
 */
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
        popup_delete(view);
    }

    popup_schedule(pm);
}

/**
 * @brief       取某一层当前优先级最高的弹窗
 * @param       level: 弹窗层级
 * @retval      弹窗视图; 无弹窗返回 NULL
 */
view_t *popup_manager_top(popup_priority_t level)
{
    popup_manager_ensure_init(&g_popup_manager);

    return popup_list_pick(popup_level_list(&g_popup_manager, level));
}

/**
 * @brief       关闭最上层的一个弹窗(page_back 用)
 * @note        先关系统级, 系统级没有时再关应用级
 * @param       无
 * @retval      被关闭的弹窗视图; 没有弹窗返回 NULL
 */
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

/**
 * @brief       判断弹窗是否正在显示(在任一 ready 链表中)
 * @param       view: 弹窗视图
 * @retval      是返回 true, 否返回 false
 */
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

/**
 * @brief       周期调度: 检查存活时间到期 + 依次调用所有弹窗的 on_update
 * @note        排队中的弹窗也会刷新 on_update
 * @param       无
 * @retval      无
 */
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
