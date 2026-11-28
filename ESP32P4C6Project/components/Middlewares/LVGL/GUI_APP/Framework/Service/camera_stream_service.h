/**
 * @file camera_stream_service.h
 * @brief 摄像头采集服务: 双缓冲采集任务 + "cam_error" 事件发布
 *
 * 线程模型:
 *  - 采集任务(core 0): 循环 mipi_cam_frame_read() 把画面写入"非显示中"的
 *    缓冲, 同时更新 ready 下标; 连续取帧失败到阈值时 app_event_post("cam_error");
 *    任务内不触碰任何 LVGL 对象。
 *  - 消费者(LVGL 任务): camera_stream_take_frame() 取走最新帧, 由页面 set 给
 *    lv_img。服务只保证"正在显示的缓冲"和"ready 的缓冲"不会被采集任务覆盖。
 *
 * 页面职责只剩显示和订阅事件, 不再持有采集任务/缓冲。
 */

#ifndef CAMERA_STREAM_SERVICE_H
#define CAMERA_STREAM_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       开流 + 分配双缓冲 + 启动采集任务(幂等)
 * @param       无
 * @retval      true:采集已启动(或已在运行); false:摄像头不可用/内存不足
 */
bool camera_service_start(void);

/**
 * @brief       停止采集任务并关流(阻塞到任务自行收尾)
 * @param       无
 * @retval      无
 */
void camera_service_stop(void);

/**
 * @brief       查询图像尺寸(即面板分辨率)
 * @param       w/h: 输出尺寸; 需在 camera_stream_start 成功后调用
 * @retval      true:有效
 */
bool camera_stream_frame_size(uint16_t *w, uint16_t *h);

/**
 * @brief       单帧字节数(RGB565)
 * @param       无
 * @retval      字节数; 未启动时返回 0
 */
uint32_t camera_stream_frame_bytes(void);

/**
 * @brief       取最新帧(只能在 LVGL 任务中调用)
 * @param       无
 * @retval      NULL:暂无新帧; 否则为可直接作 lv_img 数据源的缓冲
 */
const void *camera_stream_take_frame(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_STREAM_SERVICE_H */
