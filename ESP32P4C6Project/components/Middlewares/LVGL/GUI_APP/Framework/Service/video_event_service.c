/**
 * @file video_event_service.c
 * @brief 视频播放结束 -> app_event 桥接实现
 *
 * bridge 在 videoplay 任务上下文执行, 只投递事件, 不触碰 LVGL。
 */

#include "video_event_service.h"
#include "app_event.h"
#include "videoplay.h"

#include <stdbool.h>

/* 播放任务收尾: 在 videoplay 任务中执行 */
static void video_done_bridge(void)
{
    app_event_post("video_done");
}

void video_event_service_init(void)
{
    static bool inited = false;

    if (inited)
    {
        return;
    }

    inited = true;
    videoplay_set_done_cb(video_done_bridge);
}
