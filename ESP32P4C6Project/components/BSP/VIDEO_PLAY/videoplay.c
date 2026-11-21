/**
 * @file videoplay.c
 * @brief AVI(MJPEG) 视频播放器实现
 *
 * 播放流程参考慧勤智远 26_video_player 例程:
 *   - 读入文件头, avi_init 解析出分辨率/帧间隔/movi 位置;
 *   - 跳到第一帧数据, 逐块读取: 'dc' 帧解码显示, 'wb' 帧跳过;
 *   - esp_timer 周期定时器控制帧间隔;
 *   - 触摸屏幕任意位置退出。
 */

#include "videoplay.h"
#include "avi.h"
#include "mjpeg.h"
#include "lcd.h"
#include "touch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "videoplay";

#define FRAME_GUARD_US      500000      /* 启动后忽略触摸的时间, 避免按下播放按钮的手指立即退出 */
#define DEFAULT_FRAME_US    33333       /* 帧间隔无效时的默认值(30fps) */

static volatile bool s_active = false;      /* 播放任务运行中(LVGL 据此暂停刷新) */
static volatile bool s_stop_req = false;    /* 请求停止 */
static volatile bool s_frame_ready = false; /* 帧间隔定时器到期 */
static char s_path[256];                    /* 当前文件路径(传给任务, 避免调用者字符串失效) */
static esp_timer_handle_t s_frame_timer = NULL;

/**
 * @brief       帧间隔定时器回调
 * @param       arg: 未使用
 * @retval      无
 */
static void frame_timer_cb(void *arg)
{
    (void)arg;
    s_frame_ready = true;
}

/**
 * @brief       扫描触摸屏是否有按下
 * @param       无
 * @retval      true:有触摸
 */
static bool touch_pressed(void)
{
    tp_dev.scan(0);
    return (tp_dev.sta & TP_PRES_DOWN) != 0;
}

/**
 * @brief       启动帧间隔定时器
 * @param       period_us: 帧间隔(us)
 * @retval      ESP_OK:成功
 */
static esp_err_t frame_timer_start(uint32_t period_us)
{
    esp_timer_create_args_t args = {
        .callback = frame_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "video_frame",
    };

    if (s_frame_timer == NULL)
    {
        esp_err_t ret = esp_timer_create(&args, &s_frame_timer);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    s_frame_ready = false;
    return esp_timer_start_periodic(s_frame_timer, period_us);
}

/**
 * @brief       停止并删除帧间隔定时器
 * @param       无
 * @retval      无
 */
static void frame_timer_stop(void)
{
    if (s_frame_timer != NULL)
    {
        esp_timer_stop(s_frame_timer);
        esp_timer_delete(s_frame_timer);
        s_frame_timer = NULL;
    }
}

/**
 * @brief       播放一个 AVI 文件的完整流程
 * @param       path: 文件路径
 * @retval      无
 */
static void videoplay_task(void *arg)
{
    (void)arg;

    FILE *f = NULL;
    uint8_t *framebuf = NULL;
    uint8_t *jpeg_buf = NULL;
    uint32_t jpeg_size = 0;
    size_t nread;
    uint32_t offset;
    uint32_t period_us;
    int64_t guard_until;
    bool ok = false;

    /* 读取缓冲放 PSRAM: 单帧上限 260KB, 内部 RAM 放不下 */
    framebuf = heap_caps_malloc(AVI_MAX_FRAME_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (framebuf == NULL)
    {
        ESP_LOGE(TAG, "no memory for frame buffer");
        goto cleanup;
    }

    f = fopen(s_path, "rb");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "open %s failed", s_path);
        goto cleanup;
    }

    nread = fread(framebuf, 1, AVI_MAX_FRAME_SIZE, f);
    if (nread < 32)
    {
        ESP_LOGE(TAG, "file too short");
        goto cleanup;
    }

    if (avi_init(framebuf, (uint32_t)nread) != AVI_OK)
    {
        ESP_LOGE(TAG, "avi parse failed");
        goto cleanup;
    }

    if (g_avix.Width == 0 || g_avix.Height == 0 || g_avix.Width > 4096 || g_avix.Height > 4096)
    {
        ESP_LOGE(TAG, "unsupported video size %lux%lu",
                 (unsigned long)g_avix.Width, (unsigned long)g_avix.Height);
        goto cleanup;
    }

    period_us = g_avix.SecPerFrame ? g_avix.SecPerFrame : DEFAULT_FRAME_US;
    if (frame_timer_start(period_us) != ESP_OK)
    {
        ESP_LOGE(TAG, "frame timer start failed");
        goto cleanup;
    }

    /* 定位到 movi 中第一帧的数据 */
    offset = avi_srarch_id(framebuf, (uint32_t)nread, "movi");
    if (offset == 0)
    {
        ESP_LOGE(TAG, "movi not found");
        goto cleanup;
    }
    avi_get_streaminfo(framebuf + offset + 4);
    fseek(f, (long)(offset + 12), SEEK_SET);

    jpeg_buf = mjpegdec_init(g_avix.Width, g_avix.Height, &jpeg_size);
    if (jpeg_buf == NULL)
    {
        ESP_LOGE(TAG, "jpeg init failed");
        goto cleanup;
    }

    guard_until = esp_timer_get_time() + FRAME_GUARD_US;
    ESP_LOGI(TAG, "playing %s", s_path);

    while (!s_stop_req)
    {
        uint32_t want = g_avix.StreamSize + 8;      /* 当前块数据 + 下一块的 8 字节头 */
        bool last_chunk;

        nread = fread(framebuf, 1, want, f);
        if (nread < g_avix.StreamSize)
        {
            ESP_LOGI(TAG, "end of file");
            break;
        }
        last_chunk = (nread < want);                /* 没有下一块头了, 处理完就结束 */

        if (g_avix.StreamID == AVI_VIDS_FLAG)
        {
            if (mjpegdec_decode(framebuf, g_avix.StreamSize, jpeg_buf,
                                jpeg_size, g_avix.Width, g_avix.Height) != 0)
            {
                ESP_LOGW(TAG, "frame decode failed");
            }

            /* 等待帧间隔 */
            while (!s_frame_ready && !s_stop_req)
            {
                vTaskDelay(1);
            }
            s_frame_ready = false;
        }
        /* 音频块直接丢弃(本板无音频输出) */

        /* 触摸退出(启动后短暂屏蔽, 避免播放按钮那一次按压立即生效) */
        if (esp_timer_get_time() > guard_until && touch_pressed())
        {
            /* 等手指抬起再退出, 避免 LVGL 恢复后把这次按压当成对新列表项的点击 */
            int64_t release_deadline = esp_timer_get_time() + FRAME_GUARD_US;

            while (touch_pressed() && esp_timer_get_time() < release_deadline)
            {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            ESP_LOGI(TAG, "touch exit");
            break;
        }

        if (last_chunk)
        {
            ESP_LOGI(TAG, "end of file");
            break;
        }

        /* 解析下一块头; 失败说明数据损坏或已到末尾 */
        if (avi_get_streaminfo(framebuf + g_avix.StreamSize) != AVI_OK)
        {
            break;
        }
    }

    ok = true;

cleanup:
    frame_timer_stop();

    if (f != NULL)
    {
        fclose(f);
    }

    if (jpeg_buf != NULL)
    {
        mjpegdec_free(jpeg_buf);
    }

    if (framebuf != NULL)
    {
        heap_caps_free(framebuf);
    }

    ESP_LOGI(TAG, "playback finished (%s)", ok ? "ok" : "error");
    s_active = false;
    vTaskDelete(NULL);
}

/**
 * @brief       异步播放一个 AVI 文件
 * @param       path: 文件完整路径
 * @retval      ESP_OK 或错误码
 */
esp_err_t videoplay_play(const char *path)
{
    if (path == NULL || path[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_active)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (strlen(path) >= sizeof(s_path))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    strlcpy(s_path, path, sizeof(s_path));
    s_stop_req = false;
    s_frame_ready = false;
    s_active = true;    /* 先置位: LVGL 任务立即暂停刷新 */

    if (xTaskCreate(videoplay_task, "videoplay", 8192, NULL, 4, NULL) != pdPASS)
    {
        s_active = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/**
 * @brief       播放器是否正在播放
 * @param       无
 * @retval      true:播放中
 */
bool videoplay_active(void)
{
    return s_active;
}

/**
 * @brief       请求停止播放
 * @param       无
 * @retval      无
 */
void videoplay_stop(void)
{
    s_stop_req = true;
}
