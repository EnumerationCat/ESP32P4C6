/**
 * @file video_event_service.h
 * @brief 视频播放结束 -> app_event 的桥接服务
 *
 * BSP 的 videoplay 只暴露"播放任务结束回调"(正常结束/出错/触摸退出都会调用),
 * 本服务在 Service 层把它转成 app_event("video_done"), 页面在 on_event 里订阅,
 * 不再自己向后端注册回调。
 */

#ifndef VIDEO_EVENT_SERVICE_H
#define VIDEO_EVENT_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       注册播放结束回调(幂等, 在 pages_init_all 中调用)
 * @param       无
 * @retval      无
 */
void video_event_service_init(void);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_EVENT_SERVICE_H */
