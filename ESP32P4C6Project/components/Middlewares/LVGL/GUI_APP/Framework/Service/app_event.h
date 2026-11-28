/**
 * @file app_event.h
 * @brief 应用事件队列: 任意任务 -> LVGL任务 的通用事件通知抽象
 *
 * 用途: Model(或其它任务)产生"某事发生"的通知时, 不在原上下文里直接操作
 * LVGL, 而是 app_event_post() 入队; LVGL任务在 page_manager_tick() 里通过
 * app_event_consume() 消费(排空队列), 再用 manager_event() 派发给当前页面的 on_event。
 *
 * 约束:
 *  - event_id 必须是长生命周期的字符串(建议字符串字面量), 队列只保存指针不拷贝;
 *  - user_data 按值传递: 只放整数/错误码或长期存活对象的句柄, 绝不传栈上临时缓冲;
 *  - post 非阻塞, 队列满时丢弃该事件; 状态类事件可由页面 on_update 兜底拉取;
 *  - post 可在任意任务中调用, 但不要在中断里调用(用普通队列);
 *  - consume 只能在 LVGL 任务中调用。
 */

#ifndef APP_EVENT_H
#define APP_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_EVENT_QUEUE_LEN     16      /* 队列深度(元素: 事件ID + user_data) */

/**
 * @brief       创建事件队列(幂等, 在 page_manager_init 中调用)
 * @param       无
 * @retval      无
 */
void app_event_init(void);

/**
 * @brief       投递一个事件(任意任务, 非阻塞)
 * @param       event_id : 事件ID, 必须是字符串常量
 * @param       user_data: 事件附带参数(按值), 无则传0
 * @retval      true:已入队; false:队列未初始化或已满(事件被丢弃)
 */
bool app_event_post(const char *event_id, intptr_t user_data);

/**
 * @brief       消费队列中的事件并派发(仅LVGL任务, 由 page_manager_tick 调用)
 * @note        每个事件交给 manager_event() -> 当前页面 on_event
 * @param       无
 * @retval      无
 */
void app_event_consume(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_EVENT_H */
