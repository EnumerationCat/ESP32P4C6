/**
 * @file recorder_event_service.c
 * @brief 录音/播放完成 -> app_event 桥接实现
 *
 * 两个 bridge 都在 recorder/playback 任务上下文执行, 只投递事件,
 * 绝不触碰 LVGL。
 */

#include "recorder_event_service.h"
#include "app_event.h"
#include "recorder.h"

#include <stdbool.h>

/* 录音任务收尾(文件已关闭): 在 recorder 任务中执行 */
static void rec_done_bridge(void)
{
    app_event_post("rec_done", 0);
}

/* 播放任务收尾: 在 playback 任务中执行 */
static void play_done_bridge(void)
{
    app_event_post("play_done", 0);
}

void recorder_event_service_init(void)
{
    static bool inited = false;

    if (inited)
    {
        return;
    }

    inited = true;
    recorder_set_done_cb(rec_done_bridge);
    recorder_set_play_done_cb(play_done_bridge);
}
