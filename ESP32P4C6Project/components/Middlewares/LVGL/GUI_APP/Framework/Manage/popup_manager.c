/**
 * @file popup_manager.c
 * @brief 弹窗管理器实现: 应用级/系统级两条按优先级排序的侵入式链表
 *
 * 与 page_manager 解耦: 这里只碰弹窗对象和它们内嵌的 view_t.link,
 * 不调用任何页面的 on_pause/on_resume, 也不修改页面栈。
 */

#include "popup_manager.h"
#include "container_of.h"

/* 弹窗管理器上下文 */
typedef struct
{
    bool inited;                        /* 两条链表是否已初始化 */
    list_t app;                         /* 应用级弹窗(优先级升序) */
    list_t sys;                         /* 系统级弹窗(优先级升序) */
} popup_manager_t;

/* 全局唯一实例(静态零初始化) */
static popup_manager_t s_pm;

/* 节点 -> 弹窗 */
static inline view_t *view_of(list_head_t *node)
{
    return container_of(node, view_t, link);
}

/* 取某一层对应的链表 */
static list_t *level_list(popup_manager_t *pm, popup_level_t level)
{
    return (level == POPUP_LEVEL_SYS) ? &pm->sys : &pm->app;
}

static void manager_ensure_init(popup_manager_t *pm)
{
    if (pm->inited)
    {
        return;
    }

    list_init(&pm->app);
    list_init(&pm->sys);
    pm->inited = true;
}

void popup_manager_init(void)
{
    manager_ensure_init(&s_pm);
}

void popup_manager_open(view_t *view, popup_level_t level, uint32_t priority)
{
    popup_manager_t *pm = &s_pm;
    list_t *list;

    manager_ensure_init(pm);

    if (view == NULL || view->link.owner != NULL)
    {
        return;
    }

    list = level_list(pm, level);
    list_head_init(&view->link, priority);

    if (view->on_start != NULL)
    {
        view->on_start(view);
    }

    list_add_sorted(list, &view->link);
}

void popup_manager_close(view_t *view)
{
    popup_manager_t *pm = &s_pm;

    manager_ensure_init(pm);

    if (view == NULL || view->link.owner == NULL)
    {
        return;
    }

    if (view->link.owner != &pm->app && view->link.owner != &pm->sys)
    {
        return;
    }

    if (view->on_stop != NULL)
    {
        view->on_stop(view);
    }

    list_delete(&view->link);
}

void popup_manager_close_all(popup_level_t level)
{
    popup_manager_t *pm = &s_pm;
    list_t *list;
    list_head_t *node;

    manager_ensure_init(pm);

    list = level_list(pm, level);

    /* 从优先级最高的表尾开始关 */
    while ((node = list_last(list)) != NULL)
    {
        view_t *view = view_of(node);

        if (view->on_stop != NULL)
        {
            view->on_stop(view);
        }

        list_delete(node);
    }
}

view_t *popup_manager_top(popup_level_t level)
{
    popup_manager_t *pm = &s_pm;
    list_head_t *node;

    manager_ensure_init(pm);

    node = list_last(level_list(pm, level));

    return (node != NULL) ? view_of(node) : NULL;
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
        popup_manager_close(top);
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

    return (owner == &s_pm.app || owner == &s_pm.sys);
}

void popup_manager_tick(void)
{
    popup_manager_t *pm = &s_pm;
    list_head_t *node;

    manager_ensure_init(pm);

    list_for_each(node, &pm->app)
    {
        view_t *view = view_of(node);

        if (view->on_update != NULL)
        {
            view->on_update(view);
        }
    }

    list_for_each(node, &pm->sys)
    {
        view_t *view = view_of(node);

        if (view->on_update != NULL)
        {
            view->on_update(view);
        }
    }
}
