/**
 * @file page_manager.h
 * @brief 页面管理器: ID注册表 + 挂起栈
 *
 * 页面全部是 view_t(Framework/View/view.h), 状态用内嵌的 list_head_t 管理:
 *   ready  : 可见页面(栈, 栈顶=最后打开/最上层)
 *   paused : 被挂起的页面(打开新页时, 原顶层页 on_pause 后移到这里)
 *
 * 弹窗已拆分到 Framework/Manage/popup_manager.h, 由独立的两层优先队列管理:
 * 打开/关闭弹窗不会暂停页面, manager_gopage() 只清空应用级弹窗。
 *
 * 生命周期约定:
 *  - page_open  -> 顶层页 on_pause 并挂起, 新页 on_start
 *  - page_close -> 当前页 on_stop, 恢复挂起栈顶页 on_start
 *  - tick       -> 先刷新所有弹窗, 再驱动当前页面(ready栈顶)的 on_update
 *  - click      -> manager_click 把控件点击派发给当前页面(ready栈顶)的 on_click
 *  - event      -> manager_event 把异步事件ID派发给当前页面(ready栈顶)的 on_event
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
    bool inited;                            /* 两条链表是否已初始化 */
    list_t ready;                           /* 可见页面(栈) */
    list_t paused;                          /* 挂起页面(栈) */

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
 * @brief       返回上一级: 有弹窗先关弹窗, 否则关闭当前顶层页
 * @note        弹窗由 popup_manager 管理, 系统级优先, 其次应用级
 * @param       无
 * @retval      无
 */
void page_back(void);

/**
 * @brief       切换到指定ID的页面(go page)
 * @note        只能在LVGL任务中调用; 切换前会关闭所有应用级弹窗
 *              (系统级弹窗跨页面保留); 目标页若在挂起栈中会被提到前台
 * @param       id: 页面ID
 * @retval      true:切换成功(含已在目标页); false:ID未注册或状态异常
 */
bool manager_gopage(uint16_t id);

/**
 * @brief       向当前页面派发控件点击(输入统一入口)
 * @note        控件回调只产出 点击ID+附加参数, 由本函数转发给 ready栈顶页面 的
 *              on_click; 页面未实现 on_click 时忽略。只能在LVGL任务中调用
 * @param       click_id : 控件标识字符串(以'\0'结尾)
 * @param       user_data: 附加参数(如列表行号), 无则传0
 * @retval      true:已派发; false:无当前页面或该页未实现on_click
 */
bool manager_click(const char *click_id, intptr_t user_data);

/**
 * @brief       向当前页面派发异步事件(Controller统一入口)
 * @note        事件来自 Service/后端(经 app_event 队列), 由本函数转发给
 *              ready栈顶页面 的 on_event; 页面未实现 on_event 时忽略。
 *              只能在LVGL任务中调用(由 page_manager_tick 排空队列时驱动)
 * @param       event_id : 事件ID字符串(以'\0'结尾)
 * @param       user_data: 事件附带参数(按值), 无则传0
 * @retval      true:已派发; false:无当前页面或该页未实现on_event
 */
bool manager_event(const char *event_id, intptr_t user_data);

/**
 * @brief       当前页面(ready栈顶, 不含弹窗)
 * @param       无
 * @retval      页面指针; 无页面返回NULL
 */
view_t *page_current(void);

/**
 * @brief       周期刷新: 先刷新所有弹窗, 再驱动当前页面的 on_update
 * @note        在LVGL任务中与 lv_timer_handler() 一起调用
 * @param       无
 * @retval      无
 */
void page_manager_tick(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*PAGE_MANAGER_H*/
