/**
 * @file page_manager.c
 * @brief 页面管理器实现: ready/paused/popup 三条侵入式链表 + ID注册表
 *
 * 所有状态集中在全局唯一实例 g_page_manager 中。
 */

#include "page_manager.h"
#include "container_of.h"

/* 全局唯一的页面管理器实例(静态零初始化) */
page_manager_t g_page_manager;

/* 节点 -> 页面 */
static inline view_t *view_of(list_head_t *node)
{
    return container_of(node, view_t, link);
}

static view_t *list_top(const list_t *list)
{
    list_head_t *node = list_last(list);

    return (node != NULL) ? view_of(node) : NULL;
}

static void manager_ensure_init(page_manager_t *pm)
{
    if (pm->inited)
    {
        return;
    }

    list_init(&pm->ready);
    list_init(&pm->paused);
    list_init(&pm->popup);
    pm->inited = true;
}

/* 把当前顶层页挂起(ready -> paused), 只处理一层 */
static void suspend_top_page(page_manager_t *pm)
{
    view_t *top = list_top(&pm->ready);

    if (top == NULL)
    {
        return;
    }

    if (top->on_pause != NULL)
    {
        top->on_pause(top);
    }

    list_delete(&top->link);
    list_add_tail(&pm->paused, &top->link);
}

/* 让页面进入ready: 先挂起当前顶层页, 再启动/恢复目标页 */
static void enter_page(page_manager_t *pm, view_t *view, bool resume)
{
    suspend_top_page(pm);

    if (resume)
    {
        if (view->on_resume != NULL)
        {
            view->on_resume(view);
        }
        else if (view->on_start != NULL)
        {
            view->on_start(view);
        }
    }
    else if (view->on_start != NULL)
    {
        view->on_start(view);
    }

    list_add_tail(&pm->ready, &view->link);
}

/* 恢复挂起栈顶页(paused -> ready) */
static void resume_top_page(page_manager_t *pm)
{
    view_t *top = list_top(&pm->paused);

    if (top == NULL)
    {
        return;
    }

    list_delete(&top->link);

    if (top->on_resume != NULL)
    {
        top->on_resume(top);
    }
    else if (top->on_start != NULL)
    {
        top->on_start(top);
    }

    list_add_tail(&pm->ready, &top->link);
}

void page_manager_init(void)
{
    manager_ensure_init(&g_page_manager);
}

bool page_register(view_t *view)
{
    page_manager_t *pm = &g_page_manager;
    uint16_t i;

    if (view == NULL || view->id == 0)
    {
        return false;
    }

    /* 同一对象重复注册: 视为成功 */
    for (i = 0; i < pm->reg_count; i++)
    {
        if (pm->registry[i] == view)
        {
            return true;
        }
    }

    if (pm->reg_count >= PAGE_REGISTRY_MAX || page_find(view->id) != NULL)
    {
        return false;
    }

    pm->registry[pm->reg_count++] = view;
    return true;
}

view_t *page_find(uint16_t id)
{
    page_manager_t *pm = &g_page_manager;
    uint16_t i;

    if (id == 0)
    {
        return NULL;
    }

    for (i = 0; i < pm->reg_count; i++)
    {
        if (pm->registry[i]->id == id)
        {
            return pm->registry[i];
        }
    }

    return NULL;
}

void page_open(view_t *view)
{
    page_manager_t *pm = &g_page_manager;

    manager_ensure_init(pm);

    if (view == NULL || view->link.owner != NULL)
    {
        return;
    }

    enter_page(pm, view, false);
}

void page_close(view_t *view)
{
    page_manager_t *pm = &g_page_manager;

    manager_ensure_init(pm);

    if (view == NULL || view->link.owner != &pm->ready)
    {
        return;
    }

    if (view->on_stop != NULL)
    {
        view->on_stop(view);
    }

    list_delete(&view->link);

    /* 弹窗还在时由 popup_close 负责恢复底层页 */
    if (list_empty(&pm->popup))
    {
        resume_top_page(pm);
    }
}

void page_back(void)
{
    page_manager_t *pm = &g_page_manager;

    manager_ensure_init(pm);

    if (!list_empty(&pm->popup))
    {
        page_popup_close(list_top(&pm->popup));
        return;
    }

    page_close(list_top(&pm->ready));
}

bool manager_gopage(uint16_t id)
{
    page_manager_t *pm = &g_page_manager;
    view_t *view;

    manager_ensure_init(pm);

    view = page_find(id);
    if (view == NULL)
    {
        return false;
    }

    if (page_current() == view)
    {
        return true;
    }

    /* 切页前收掉所有弹窗(从最高优先级开始) */
    while (!list_empty(&pm->popup))
    {
        page_popup_close(list_top(&pm->popup));
    }

    /* 关闭弹窗可能已经把目标页恢复到前台 */
    if (page_current() == view)
    {
        return true;
    }

    /* 目标页在挂起栈里: 先摘出来, 走恢复流程(on_resume) */
    if (view->link.owner == &pm->paused)
    {
        list_delete(&view->link);
        enter_page(pm, view, true);
        return true;
    }

    if (view->link.owner != NULL)
    {
        return false;
    }

    enter_page(pm, view, false);
    return true;
}

void page_popup_open(view_t *view, uint32_t priority)
{
    page_manager_t *pm = &g_page_manager;

    manager_ensure_init(pm);

    if (view == NULL || view->link.owner != NULL)
    {
        return;
    }

    suspend_top_page(pm);

    list_head_init(&view->link, priority);

    if (view->on_start != NULL)
    {
        view->on_start(view);
    }

    list_add_sorted(&pm->popup, &view->link);
}

void page_popup_close(view_t *view)
{
    page_manager_t *pm = &g_page_manager;

    manager_ensure_init(pm);

    if (view == NULL || view->link.owner != &pm->popup)
    {
        return;
    }

    if (view->on_stop != NULL)
    {
        view->on_stop(view);
    }

    list_delete(&view->link);

    if (list_empty(&pm->popup))
    {
        resume_top_page(pm);
    }
}

view_t *page_current(void)
{
    page_manager_t *pm = &g_page_manager;

    manager_ensure_init(pm);

    if (!list_empty(&pm->popup))
    {
        return list_top(&pm->popup);
    }

    return list_top(&pm->ready);
}

void page_manager_tick(void)
{
    page_manager_t *pm = &g_page_manager;
    view_t *top;

    manager_ensure_init(pm);

    top = page_current();
    if (top != NULL && top->on_update != NULL)
    {
        top->on_update(top);
    }
}
