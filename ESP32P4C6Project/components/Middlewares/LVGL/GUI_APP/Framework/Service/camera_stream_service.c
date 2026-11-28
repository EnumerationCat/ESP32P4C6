/**
 * @file camera_stream_service.c
 * @brief 摄像头双缓冲采集实现
 *
 * 采集任务与显示方(LVGL 任务)通过 ready/disp 两个下标交接缓冲:
 *  - 采集任务只在 ready < 0 时写入 1 - disp 缓冲;
 *  - 显示方取走帧时先更新 disp, 再清 ready, 保证采集任务不会立刻覆盖刚取走的缓冲。
 */

#include "camera_stream_service.h"
#include "app_event.h"
#include "mipi_cam.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "cam_stream";

#define CAM_STREAM_TASK_STACK   4096
#define CAM_ERR_THRESHOLD       30      /* 连续取帧失败次数阈值(约300ms) */

static uint8_t *s_buf[2];               /* 双缓冲: 面板分辨率 RGB565 */
static size_t s_buf_size;               /* 单缓冲大小(已按cache行对齐) */
static uint32_t s_data_size;            /* 单帧字节数(w*h*2) */
static uint16_t s_w;
static uint16_t s_h;

static TaskHandle_t s_task;             /* 采集任务句柄, 退出后置NULL */
static volatile bool s_running;         /* 采集任务运行标志 */
static volatile int s_ready = -1;       /* 采集任务刚写完的缓冲下标(-1=无新帧) */
static volatile int s_disp;             /* 当前显示中的缓冲下标 */

/**
 * @brief       分配双缓冲(只在首次进入时分配, 之后复用)
 * @param       无
 * @retval      true:缓冲可用
 */
static bool buffers_alloc(void)
{
    uint16_t w;
    uint16_t h;

    if (!mipi_cam_frame_size(&w, &h))
    {
        return false;
    }

    s_w = w;
    s_h = h;
    s_buf_size = mipi_cam_frame_buf_size();
    s_data_size = (uint32_t)w * h * 2;

    if (s_buf[0] != NULL && s_buf[1] != NULL)
    {
        return true;
    }

    size_t align = mipi_cam_frame_buf_align();

    for (int i = 0; i < 2; i++)
    {
        s_buf[i] = heap_caps_aligned_alloc(align, s_buf_size,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_buf[i] == NULL)
        {
            ESP_LOGE(TAG, "alloc frame buffer %d failed (%u bytes)", i, (unsigned)s_buf_size);
            heap_caps_free(s_buf[0]);
            s_buf[0] = NULL;
            return false;
        }
    }

    return true;
}

/**
 * @brief       采集任务: 把画面写进"非显示中"的缓冲, 等 LVGL 任务来取
 * @param       arg: 未使用
 * @retval      无
 */
static void capture_task(void *arg)
{
    int fails = 0;              /* 连续取帧失败次数 */
    bool err_reported = false;  /* 本轮错误是否已上报 */

    (void)arg;

    while (s_running)
    {
        /* 上一帧还没被取走时先等: 保证显示中的缓冲和 ready 的缓冲都不会被覆盖 */
        if (s_ready >= 0)
        {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        int idx = 1 - s_disp;

        if (mipi_cam_frame_read(s_buf[idx], s_buf_size) == ESP_OK)
        {
            s_ready = idx;
            fails = 0;
            err_reported = false;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10));

            /* 连续失败到阈值才上报一次, 避免刷屏; 恢复出帧后允许再次上报 */
            if (++fails >= CAM_ERR_THRESHOLD && !err_reported)
            {
                err_reported = true;
                app_event_post("cam_error", 0);
            }
        }
    }

    mipi_cam_frame_end();
    s_task = NULL;
    vTaskDelete(NULL);
}

bool camera_service_start(void)
{
    if (s_task != NULL)
    {
        return true;
    }

    if (!buffers_alloc())
    {
        ESP_LOGE(TAG, "camera not ready");
        return false;
    }

    if (mipi_cam_frame_begin() != ESP_OK)
    {
        ESP_LOGE(TAG, "camera stream start failed");
        return false;
    }

    s_ready = -1;
    s_disp  = 0;
    s_running = true;

    if (xTaskCreatePinnedToCore(capture_task, "cam stream", CAM_STREAM_TASK_STACK,
                                NULL, 4, &s_task, 0) != pdPASS)
    {
        ESP_LOGE(TAG, "create capture task failed");
        s_running = false;
        mipi_cam_frame_end();
        return false;
    }

    return true;
}

void camera_service_stop(void)
{
    s_running = false;

    for (int i = 0; i < 100 && s_task != NULL; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_task != NULL)
    {
        /* 阻塞在取帧里: 关流让 DQBUF 返回错误, 再等它退出 */
        mipi_cam_frame_end();
        for (int i = 0; i < 50 && s_task != NULL; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

const void *camera_stream_take_frame(void)
{
    int idx = s_ready;

    if (idx < 0)
    {
        return NULL;
    }

    /* 先登记新的显示缓冲, 再清 ready: 否则采集任务可能马上覆盖刚取走的那块 */
    s_disp  = idx;
    s_ready = -1;

    return s_buf[idx];
}

bool camera_stream_frame_size(uint16_t *w, uint16_t *h)
{
    if (w == NULL || h == NULL || s_w == 0 || s_h == 0)
    {
        return false;
    }

    *w = s_w;
    *h = s_h;
    return true;
}

uint32_t camera_stream_frame_bytes(void)
{
    return s_data_size;
}
