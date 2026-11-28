/**
 * @file recorder_event_service.h
 * @brief 录音/播放完成 -> app_event 的桥接服务
 *
 * BSP 的 recorder 只暴露"任务结束回调", 不认识 GUI 事件层。本服务在
 * Service 层注册该回调, 把它转成 app_event("rec_done"/"play_done"),
 * 页面只需在 on_event 里订阅事件, 不再自己向后端注册回调。
 */

#ifndef RECORDER_EVENT_SERVICE_H
#define RECORDER_EVENT_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       注册录音/播放完成回调(幂等, 在 pages_init_all 中调用)
 * @param       无
 * @retval      无
 */
void recorder_event_service_init(void);

#ifdef __cplusplus
}
#endif

#endif /* RECORDER_EVENT_SERVICE_H */
