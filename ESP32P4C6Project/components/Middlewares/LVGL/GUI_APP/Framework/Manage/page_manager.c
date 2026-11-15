/**
 * @file page_manager.c
 * @brief 页面管理器实现: ready/paused/popup 三条侵入式链表 + ID注册表
 */

#include "page_manager.h"
#include "container_of.h"

static bool s_inited;
static list_t s_ready;          /* 可见页面(栈) */
static list_t s_paused;         /* 挂起页面(栈) */
static list_t s_popup;          /* 弹窗(按优先级升序) */

static view_t *s_registry[PAGE_REGISTRY_MAX];
static uint16_t s_reg_count;

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

static void manager_ensure_init(void)
{
    if (s_inited)
    {
        return;
    }

    list_init(&s_ready);
    list_init(&s_paused);
    list_init(&s_popup);
    s_inited = true;
}

/* 把当前顶层页挂起(ready -> paused), 只处理一层 */
static void suspend_top_page(void)
{
    view_t *top = list_top(&s_ready);

    if (top == NULL)
    {
        return;
    }

    if (top->on_pause != NULL)
    {
        top->on_pause(top);
    }

    list_del(&top->link);
    list_add_tail(&s_paused, &top->link);
}

/* 恢复挂起栈顶页(paused -> ready) */
static void resume_top_page(void)
{
    view_t *top = list_top(&s_paused);

    if (top == NULL)
    {
        return;
    }

    list_del(&top->link);

    if (top->on_start != NULL)
    {
        top->on_start(top);
    }

    list_add_tail(&s_ready, &top->link);
}

void page_manager_init(void)
{
    manager_ensure_init();
}

bool page_register(view_t *view)
{
    uint16_t i;

    if (view == NULL || view->id == 0)
    {
        return false;
    }

    /* 同一对象重复注册: 视为成功 */
    for (i = 0; i < s_reg_count; i++)
    {
        if (s_registry[i] == view)
        {
            return true;
        }
    }

    if (s_reg_count >= PAGE_REGISTRY_MAX || page_find(view->id) != NULL)
    {
        return false;
    }

    s_registry[s_reg_count++] = view;
    return true;
}

view_t *page_find(uint16_t id)
{
    uint16_t i;

    if (id == 0)
    {
        return NULL;
    }

    for (i = 0; i < s_reg_count; i++)
    {
        if (s_registry[i]->id == id)
        {
            return s_registry[i];
        }
    }

    return NULL;
}

void page_open(view_t *view)
{
    manager_ensure_init();

    if (view == NULL || view->link.owner != NULL)
    {
        return;
    }

    suspend_top_page();

    if (view->on_start != NULL)
    {
        view->on_start(view);
    }

    list_add_tail(&s_ready, &view->link);
}

void page_close(view_t *view)
{
    manager_ensure_init();

    if (view == NULL || view->link.owner != &s_ready)
    {
        return;
    }

    if (view->on_stop != NULL)
    {
        view->on_stop(view);
    }

    list_del(&view->link);

    /* 弹窗还在时由 popup_close 负责恢复底层页 */
    if (list_empty(&s_popup))
    {
        resume_top_page();
    }
}

void page_back(void)
{
    manager_ensure_init();

    if (!list_empty(&s_popup))
    {
        page_popup_close(list_top(&s_popup));
        return;
    }

    page_close(list_top(&s_ready));
}

void page_popup_open(view_t *view, uint32_t priority)
{
    manager_ensure_init();

    if (view == NULL || view->link.owner != NULL)
    {
        return;
    }

    suspend_top_page();

    list_head_init(&view->link, priority);

    if (view->on_start != NULL)
    {
        view->on_start(view);
    }

    list_add_sorted(&s_popup, &view->link);
}

void page_popup_close(view_t *view)
{
    manager_ensure_init();

    if (view == NULL || view->link.owner != &s_popup)
    {
        return;
    }

    if (view->on_stop != NULL)
    {
        view->on_stop(view);
    }

    list_del(&view->link);

    if (list_empty(&s_popup))
    {
        resume_top_page();
    }
}

view_t *page_current(void)
{
    manager_ensure_init();

    if (!list_empty(&s_popup))
    {
        return list_top(&s_popup);
    }

    return list_top(&s_ready);
}

void page_manager_tick(void)
{
    view_t *top;

    manager_ensure_init();

    top = page_current();
    if (top != NULL && top->on_update != NULL)
    {
        top->on_update(top);
    }
}
