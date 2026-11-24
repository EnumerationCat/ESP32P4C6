/**
 * @file popup_manager.h
 * @brief 弹窗管理器(Framework/Manage): 模仿 FreeRTOS 任务的抢占式调度
 *
 * 与 page_manager 的分工:
 *   page_manager  : 只管理页面(ready/paused), 负责页面切换与挂起恢复;
 *   popup_manager : 只管理弹窗, 打开/关闭弹窗不会暂停页面, 也不改变页面栈。
 *
 * 调度模型(两条不同优先级的 ready 链表, sys 整层高于 app):
 *   POPUP_LEVEL_APP: 应用级, 随页面切换由 manager_gopage() 清空;
 *   POPUP_LEVEL_SYS: 系统级, 跨页面保留, 任何 sys 弹窗都能抢占 app 弹窗。
 *
 *   - 同一时刻只有一个弹窗在"运行"(已调 on_start, 显示中), 其余在链表中排队;
 *   - 抢占: 新弹窗严格高于当前运行者(先层级后 priority)时, 当前弹窗被直接
 *           关闭(调 on_stop 销毁控件并出队), 高优先级退出后不再恢复;
 *   - 同优先级: 不抢占, 后来的 FIFO 排队, 等前面的退出后轮到自己;
 *   - 存活时间: popup_base.value(ms), 只在运行时倒计时;
 *           到点调 request_close 让弹窗退场, 0 表示常驻。
 *
 * 每条链表都按 priority 升序排列, 表尾优先级最高。
 * 管理器只串指针、不负责内存: 动态实例(每次 show 新建)由弹窗在
 * popup_base.request_delete 中自行释放, 静态实例保持 request_delete=NULL。
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

/* 弹窗管理器(全局唯一实例 g_popup_manager, 定义在 popup_manager.c) */
typedef struct
{
    bool     inited;                    /* 两条链表是否已初始化 */
    list_t   app;                       /* 应用级 ready 链表(priority 升序) */
    list_t   sys;                       /* 系统级 ready 链表(priority 升序) */
    view_t  *running;                   /* 当前运行的弹窗; NULL=空闲 */
    uint32_t run_tick;                  /* 本次运行开始的时间戳(存活时间起点) */
    bool     expiring;                  /* running 已到期, 正在播退场动画 */

    /* 统一的弹窗生命周期方法(统一走 g_popup_manager.create / .destroy):
     *   create : 按模板分配实例 + 记录层级/优先级 + 上架(入队调度), 返回基类指针;
     *   destroy: 停止(运行中先 on_stop) + 出队 + 释放动态实例内存。 */
    struct popup_base *(*create)(const struct popup_base *prototype, uint32_t size,
                                 popup_priority_t level, uint32_t priority);
    void (*destroy)(view_t *view);
} popup_manager_t;

/* 全局唯一的弹窗管理器实例(定义在 popup_manager.c) */
extern popup_manager_t g_popup_manager;






/**
 * @brief       初始化弹窗管理器(可重复调用)
 * @param       无
 * @retval      无
 */
void popup_manager_init(void);

/*
 * 统一的生命周期入口(方法, 便于调用方一眼看全):
 *   1) 先准备一份"原型"(模板的浅拷贝, 把派生数据填好, 如文字/回调);
 *   2) create 会按原型分配一个新实例并上架(入队调度), 原型可以是栈上局部变量:
 *
 *        popup_notice_ui_t prototype = s_notice_template;   // 已填好数据
 *        popup_notice_ui_t *ui = (popup_notice_ui_t *)
 *            g_popup_manager.create(&prototype.base, sizeof(prototype),
 *                                   POPUP_LEVEL_SYS, SYSTEM_PRIORITY0);
 *
 *      create 返回 NULL 表示分配失败; 返回值第一个成员是 popup_base_t。
 *      create 内部会立刻(或在排到队首时)调用 on_start, 因此派生数据必须在
 *      create 之前填好(所以走"原型"), 否则 on_start 拿到的是空数据。
 *
 *   3) 需要主动结束时:
 *
 *        g_popup_manager.destroy(&ui->base.view);
 *
 *      destroy = 停止(运行中先 on_stop 销毁控件) + 出队 + 释放实例内存(动态实例)。
 *      静态实例(request_delete==NULL)只出队不释放。
 *
 *   注意: 管理器的释放会先调用弹窗的 popup_base.request_delete 做类型清理(如从
 *         本类型实例链表摘除), 再统一 lv_mem_free, 所以 request_delete 里不要再释放内存。
 *   两者都只能在 LVGL 任务中调用。
 */

/**
 * @brief       按原型创建并上架一个系统级弹窗(create 的便捷封装)
 * @note        等价于 g_popup_manager.create(prototype, size, POPUP_LEVEL_SYS, priority);
 *              跨页面保留, 控件应绘制在 lv_layer_sys()
 * @param       prototype:    实例模板(整块派生结构的原型)
 * @param       size:     实例总大小(字节, 至少为 sizeof(popup_base_t))
 * @param       priority: 系统级队列内优先级(数值越大越靠上)
 * @retval      新的弹窗基类指针; 分配失败返回NULL
 */
struct popup_base *system_popup_create(const struct popup_base *prototype,
                                       uint32_t size,
                                       systempopup_priority_t priority);

/**
 * @brief       按原型创建并上架一个应用级弹窗(create 的便捷封装)
 * @note        等价于 g_popup_manager.create(prototype, size, POPUP_LEVEL_APP, priority);
 *              随页面切换关闭, 控件应绘制在 lv_layer_top()
 * @param       prototype:    实例模板(整块派生结构的原型)
 * @param       size:     实例总大小(字节, 至少为 sizeof(popup_base_t))
 * @param       priority: 应用级队列内优先级(数值越大越靠上)
 * @retval      新的弹窗基类指针; 分配失败返回NULL
 */
struct popup_base *app_popup_create(const struct popup_base *prototype,
                                    uint32_t size,
                                    apppopup_priority_t priority);

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
 * @brief       周期调度: 检查存活时间到期 + 依次调用所有弹窗的 on_update
 * @note        排队中的弹窗也会刷新 on_update; on_update 里不要关闭弹窗
 * @param       无
 * @retval      无
 */
void popup_manager_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* POPUP_MANAGER_H */
