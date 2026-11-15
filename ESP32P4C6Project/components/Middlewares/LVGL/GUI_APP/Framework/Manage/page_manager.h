/**
 * @file page_manager.h
 * @brief 页面管理器: ID注册表 + 挂起栈 + 弹窗优先级
 *
 * 页面全部是 view_t(Framework/View/view.h), 状态用内嵌的 list_head_t 管理:
 *   ready  : 可见页面(栈, 栈顶=最后打开/最上层)
 *   paused : 被挂起的页面(打开新页/弹窗时, 原顶层页 on_pause 后移到这里)
 *   popup  : 弹窗, 按 link.value(=优先级) 升序, 表尾=优先级最高
 *
 * 生命周期约定:
 *  - page_open    -> 顶层页 on_pause 并挂起, 新页 on_start
 *  - page_close   -> 当前页 on_stop, 恢复挂起栈顶页 on_start
 *  - popup_open   -> 底层页 on_pause, 弹窗 on_start 后按优先级插入
 *  - popup_close  -> 弹窗 on_stop; 所有弹窗关闭后恢复底层页
 *  - tick         -> 只驱动"当前可见页"(最高优先级弹窗, 否则 ready栈顶) 的 on_update
 *
 * 页面对象必须长期存活(静态/全局), 管理器只串指针, 不负责内存。
 */

#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "view.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAGE_REGISTRY_MAX   16      /* 注册表容量 */

/* 页面管理器(全局唯一实例g_page_manager) */
typedef struct
{
    bool inited;                            /* 三条链表是否已初始化 */
    list_t ready;                           /* 可见页面(栈) */
    list_t paused;                          /* 挂起页面(栈) */
    list_t popup;                           /* 弹窗(按优先级升序) */

    view_t *registry[PAGE_REGISTRY_MAX];    /* 全部页面(按ID查找) */
    uint16_t reg_count;
} page_manager_t;

/* 全局唯一的页面管理器实例(定义在page_manager.c) */
extern page_manager_t g_page_manager;

/**
 * @brief       初始化管理器(可重复调用)
 * @param       无
 * @retval      无
 */
void page_manager_init(void);

/**
 * @brief       注册页面(按id查找)
 * @note        id为0或表满或同id冲突时返回false
 * @param       view: 页面(其id/name字段需已初始化)
 * @retval      true:成功
 */
bool page_register(view_t *view);

/**
 * @brief       按ID查找页面
 * @param       id: 页面ID(注册时的view->id)
 * @retval      页面指针; 未找到返回NULL
 */
view_t *page_find(uint16_t id);

/**
 * @brief       打开页面: 挂起当前顶层页, 启动新页
 * @note        只能在LVGL任务中调用; 页面已在某链表上时忽略
 * @param       view: 页面
 * @retval      无
 */
void page_open(view_t *view);

/**
 * @brief       关闭页面: on_stop并恢复挂起栈顶页
 * @note        只能在LVGL任务中调用
 * @param       view: 页面
 * @retval      无
 */
void page_close(view_t *view);

/**
 * @brief       关闭当前顶层页(返回上一页)
 * @param       无
 * @retval      无
 */
void page_back(void);

/**
 * @brief       切换到指定ID的页面(go page)
 * @note        只能在LVGL任务中调用; 切换前会关闭所有弹窗;
 *              目标页若在挂起栈中会被提到前台
 * @param       id: 页面ID
 * @retval      true:切换成功(含已在目标页); false:ID未注册或状态异常
 */
bool manager_gopage(uint16_t id);

/**
 * @brief       打开弹窗(带优先级, 数值越大越靠上)
 * @param       view:     弹窗页面
 * @param       priority: 优先级(写入 link.value)
 * @retval      无
 */
void page_popup_open(view_t *view, uint32_t priority);

/**
 * @brief       关闭弹窗
 * @param       view: 弹窗页面
 * @retval      无
 */
void page_popup_close(view_t *view);

/**
 * @brief       当前可见页(最高优先级弹窗, 否则ready栈顶)
 * @param       无
 * @retval      页面指针; 无可见页返回NULL
 */
view_t *page_current(void);

/**
 * @brief       周期刷新: 只驱动当前可见页的 on_update
 * @note        在LVGL任务中与 lv_timer_handler() 一起调用
 * @param       无
 * @retval      无
 */
void page_manager_tick(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*PAGE_MANAGER_H*/
