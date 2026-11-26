/**
 * @file camera_page.c
 * @brief 相机页: 摄像头画面经 PPA 缩放到 PSRAM 双缓冲, 由 lv_img 显示
 *
 * 与其它页面相同的 C 式继承: base 必须是第一个成员。
 *
 * 线程模型:
 *  - 采集任务(core 0): 循环 mipi_cam_frame_read() 把画面写入"非显示中"的缓冲,
 *    同时更新 ready_idx; 不碰任何 LVGL 对象。
 *  - LVGL 定时器: 有新帧时切换 dsc.data 指针并 invalidate, 由 LVGL 任务真正渲染。
 * 因此 LVGL 永远显示 disp_idx 缓冲, 采集任务永远写另一个缓冲, 不会撕裂。
 */

#include "camera_page.h"

#include "user_home_page.h"
#include "page_manager.h"
#include "view.h"
#include "container_of.h"
#include "mipi_cam.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "camera_page";

#define CAM_PAGE_TIMER_MS   20      /* LVGL 取新帧的轮询周期(ms) */
#define CAM_PAGE_TASK_STACK 4096

/* 派生视图: base 必须是第一个成员(C式继承) + 界面对象和采集状态 */
typedef struct
{
    view_t base;                    /* Framework基类(必须放第一位) */

    lv_obj_t *screen;               /* 相机页屏幕 */
    lv_obj_t *img;                  /* 显示摄像头画面的 lv_img */
    lv_obj_t *tip;                  /* 摄像头不可用时的提示 */
    lv_img_dsc_t dsc;               /* 指向当前显示缓冲的图像描述符 */
    lv_timer_t *timer;              /* 新帧轮询定时器(LVGL任务) */

    TaskHandle_t cap_task;          /* 采集任务句柄, 退出后置NULL */
    volatile bool running;          /* 采集任务运行标志 */
    volatile int ready_idx;         /* 采集任务刚写完的缓冲下标(-1=无新帧) */
    volatile int disp_idx;          /* 当前显示中的缓冲下标 */

    uint8_t *buf[2];                /* 双缓冲: 面板分辨率 RGB565 */
    size_t buf_size;                /* 单缓冲大小(已按cache行对齐) */
    bool src_set;                   /* 是否已把 dsc 设置给 lv_img(首帧后才设置) */
} camera_page_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void camera_page_create(view_t *self);
static void camera_page_destroy(view_t *self);
static void camera_page_on_start(view_t *self);
static void camera_page_on_pause(view_t *self);
static void camera_page_on_resume(view_t *self);
static void camera_page_on_stop(view_t *self);

static void cam_capture_task(void *arg);
static void cam_frame_timer_cb(lv_timer_t *timer);
static void cam_capture_start(void);
static void cam_capture_stop(void);

static camera_page_ui_t s_ui =
{
    .base =
    {
        .id        = CAMERA_PAGE_ID,
        .name      = "camera",
        .init      = camera_page_create,
        .deinit    = camera_page_destroy,
        .on_start  = camera_page_on_start,
        .on_pause  = camera_page_on_pause,
        .on_resume = camera_page_on_resume,
        .on_stop   = camera_page_on_stop,
    },
    .dsc =
    {
        .header = { .cf = LV_IMG_CF_TRUE_COLOR, .w = 0, .h = 0 },
        .data_size = 0,
        .data = NULL,
    },
    .ready_idx = -1,
};

/* 由基类指针还原派生视图 */
static inline camera_page_ui_t *camera_ui(view_t *self)
{
    return container_of(self, camera_page_ui_t, base);
}

/**********************
 *   采集
 **********************/

/* 采集任务: 把画面写进"非显示中"的缓冲, 等 LVGL 定时器来取 */
static void cam_capture_task(void *arg)
{
    camera_page_ui_t *ui = (camera_page_ui_t *)arg;

    while (ui->running)
    {
        /* 上一帧还没被 LVGL 取走时先等: 保证"正在显示的缓冲"和"ready 的缓冲"
         * 都不会被采集任务覆盖, 只在两者之外的那个缓冲上写 */
        if (ui->ready_idx >= 0)
        {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        int idx = 1 - ui->disp_idx;

        if (mipi_cam_frame_read(ui->buf[idx], ui->buf_size) == ESP_OK)
        {
            ui->ready_idx = idx;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    mipi_cam_frame_end();
    ui->cap_task = NULL;
    vTaskDelete(NULL);
}

/* LVGL 定时器: 有新帧就切换数据源并重绘 */
static void cam_frame_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    int idx = s_ui.ready_idx;

    if (idx < 0 || s_ui.img == NULL)
    {
        return;
    }

    s_ui.ready_idx = -1;
    s_ui.disp_idx  = idx;
    s_ui.dsc.data  = s_ui.buf[idx];

    if (!s_ui.src_set)
    {
        /* 首帧到达才把数据源交给 lv_img, 避免用 NULL 数据渲染 */
        lv_img_set_src(s_ui.img, &s_ui.dsc);
        s_ui.src_set = true;
        return;
    }

    lv_img_cache_invalidate_src(&s_ui.dsc);     /* 外部可变缓冲必须手动失效缓存 */
    lv_obj_invalidate(s_ui.img);
}

/* 分配双缓冲(只在首次进入时分配, 之后复用) */
static bool cam_buffers_alloc(void)
{
    uint16_t w;
    uint16_t h;

    if (!mipi_cam_frame_size(&w, &h))
    {
        return false;
    }

    s_ui.buf_size = mipi_cam_frame_buf_size();
    s_ui.dsc.header.w  = w;
    s_ui.dsc.header.h  = h;
    s_ui.dsc.data_size = (uint32_t)w * h * 2;

    if (s_ui.buf[0] != NULL && s_ui.buf[1] != NULL)
    {
        return true;
    }

    size_t align = mipi_cam_frame_buf_align();

    for (int i = 0; i < 2; i++)
    {
        s_ui.buf[i] = heap_caps_aligned_alloc(align, s_ui.buf_size,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_ui.buf[i] == NULL)
        {
            ESP_LOGE(TAG, "alloc frame buffer %d failed (%u bytes)", i, (unsigned)s_ui.buf_size);
            heap_caps_free(s_ui.buf[0]);
            s_ui.buf[0] = NULL;
            return false;
        }
    }

    return true;
}

/* 进入采集: 开流 + 起采集任务 + 起LVGL定时器 */
static void cam_capture_start(void)
{
    if (s_ui.cap_task != NULL)
    {
        return;
    }

    if (!cam_buffers_alloc())
    {
        ESP_LOGE(TAG, "camera not ready");
        return;
    }

    if (mipi_cam_frame_begin() != ESP_OK)
    {
        ESP_LOGE(TAG, "camera stream start failed");
        return;
    }

    s_ui.ready_idx = -1;
    s_ui.disp_idx  = 0;
    s_ui.running   = true;

    if (xTaskCreatePinnedToCore(cam_capture_task, "cam page", CAM_PAGE_TASK_STACK,
                                &s_ui, 4, &s_ui.cap_task, 0) != pdPASS)
    {
        ESP_LOGE(TAG, "create capture task failed");
        s_ui.running = false;
        mipi_cam_frame_end();
        return;
    }

    s_ui.timer = lv_timer_create(cam_frame_timer_cb, CAM_PAGE_TIMER_MS, NULL);
}

/* 停止采集: 等采集任务自行收尾(STREAMOFF 在它自己的任务里做) */
static void cam_capture_stop(void)
{
    s_ui.running = false;

    for (int i = 0; i < 100 && s_ui.cap_task != NULL; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_ui.cap_task != NULL)
    {
        /* 阻塞在取帧里: 关流让 DQBUF 返回错误, 再等它退出 */
        mipi_cam_frame_end();
        for (int i = 0; i < 50 && s_ui.cap_task != NULL; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (s_ui.timer != NULL)
    {
        lv_timer_del(s_ui.timer);
        s_ui.timer = NULL;
    }
}

/**********************
 *   界面
 **********************/

/* 返回主界面 */
static void back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    manager_gopage(USER_HOME_PAGE_ID);
}

/* 创建相机页控件(幂等) */
static void camera_page_create(view_t *self)
{
    camera_page_ui_t *ui = camera_ui(self);

    if (ui->screen != NULL)
    {
        return;
    }

    ui->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui->screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(ui->screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 摄像头画面: 尺寸由 dsc.header 决定, 首次取到帧后才设置 src */
    ui->img = lv_img_create(ui->screen);
    lv_obj_align(ui->img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(ui->img, LV_OBJ_FLAG_SCROLLABLE);

    /* 返回按钮叠在画面上层 */
    lv_obj_t *back_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(back_btn, 64, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 8, 8);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

/* 销毁相机页控件 */
static void camera_page_destroy(view_t *self)
{
    camera_page_ui_t *ui = camera_ui(self);

    if (ui->screen == NULL)
    {
        return;
    }

    lv_obj_del(ui->screen);
    ui->screen  = NULL;
    ui->img     = NULL;
    ui->tip     = NULL;
    ui->src_set = false;
}

/**********************
 *   页面生命周期
 **********************/

/* 首次进入: 建界面 + 启动采集 */
static void camera_page_on_start(view_t *self)
{
    camera_page_ui_t *ui = camera_ui(self);

    camera_page_create(self);
    lv_scr_load(ui->screen);
    lv_obj_invalidate(ui->screen);

    cam_capture_start();

    if (ui->cap_task == NULL && ui->tip == NULL)
    {
        /* 摄像头不可用: 在黑色背景上给个提示 */
        ui->tip = lv_label_create(ui->screen);
        lv_label_set_text(ui->tip, "Camera not available");
        lv_obj_set_style_text_color(ui->tip, lv_color_white(), LV_PART_MAIN);
        lv_obj_center(ui->tip);
    }
}

/* 被其它页覆盖: 暂停采集, 省电也避免两次抢摄像头 */
static void camera_page_on_pause(view_t *self)
{
    (void)self;
    cam_capture_stop();
}

/* 恢复: 重新开流 */
static void camera_page_on_resume(view_t *self)
{
    camera_page_ui_t *ui = camera_ui(self);

    if (ui->screen != NULL)
    {
        lv_scr_load(ui->screen);
        lv_obj_invalidate(ui->screen);
    }

    cam_capture_start();
}

/* 离开页面: 停采集并销毁控件(缓冲保留复用) */
static void camera_page_on_stop(view_t *self)
{
    cam_capture_stop();
    camera_page_destroy(self);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void camera_page_init(void)
{
    page_manager_init();
    page_register(&s_ui.base);
}
