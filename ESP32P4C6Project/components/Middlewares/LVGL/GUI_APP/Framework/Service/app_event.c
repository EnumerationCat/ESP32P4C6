/**
 * @file app_event.c
 * @brief 应用事件队列实现: 固定长度FreeRTOS队列, 存放事件ID指针
 *
 * 生产者在其它任务中调用 app_event_post(), 消费者(LVGL任务)在
 * page_manager_tick() 中调用 app_event_consume() 取出并派发, 这样跨任务
 * 通知不会在非LVGL任务里触碰任何 lv_* 接口。
 */

#include "app_event.h"
#include "page_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* 队列元素: 事件ID(指针, 长生命周期) + 附带参数(按值) */
typedef struct
{
    const char *event_id;
    intptr_t    user_data;
} app_event_item_t;

static QueueHandle_t s_queue = NULL;

void app_event_init(void)
{
    if (s_queue != NULL)
    {
        return;
    }

    s_queue = xQueueCreate(APP_EVENT_QUEUE_LEN, sizeof(app_event_item_t));
}

bool app_event_post(const char *event_id, intptr_t user_data)
{
    app_event_item_t item;

    if (s_queue == NULL || event_id == NULL)
    {
        return false;
    }

    item.event_id  = event_id;
    item.user_data = user_data;

    return xQueueSend(s_queue, &item, 0) == pdTRUE;
}

void app_event_consume(void)
{
    app_event_item_t item;

    if (s_queue == NULL)
    {
        return;
    }

    while (xQueueReceive(s_queue, &item, 0) == pdTRUE)
    {
        manager_event(item.event_id, item.user_data);
    }
}
