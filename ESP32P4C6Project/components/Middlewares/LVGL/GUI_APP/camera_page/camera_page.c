/**
 * @file camera_page.c
 * @brief 相机页: 显示 camera_stream_service 提供的摄像头帧, 叠加返回按钮
 *
 * 与其它页面相同的 C 式继承: base 必须是第一个成员。
 *
 * 分层:
 *  - 采集任务/双缓冲/异常检测在 Framework/Service/camera_stream_service;
 *  - 本页只负责显示(LVGL 定时器取最新帧喂给 lv_img)和订阅 "cam_error" 事件。
 */

#include "camera_page.h"

#include "user_home_page.h"
#include "page_manager.h"
#include "view.h"
#include "container_of.h"
#include "camera_stream_service.h"
#include "lvgl.h"

#include <string.h>

#define CAM_PAGE_TIMER_MS   20      /* LVGL 取新帧的轮询周期(ms) */

/* 派生视图: base 必须是第一个成员(C式继承) + 界面对象 */
typedef struct
{
    view_t base;                    /* Framework基类(必须放第一位) */

    lv_obj_t *screen;               /* 相机页屏幕 */
    lv_obj_t *img;                  /* 显示摄像头画面的 lv_img */
    lv_obj_t *tip;                  /* 摄像头不可用/异常时的提示 */
    lv_img_dsc_t dsc;               /* 指向当前显示缓冲的图像描述符 */
    lv_timer_t *timer;              /* 新帧轮询定时器(LVGL任务) */
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
static void camera_page_on_event(view_t *self, const char *event_id);

static void cam_frame_timer_cb(lv_timer_t *timer);
static bool cam_display_start(camera_page_ui_t *ui);
static void cam_display_stop(camera_page_ui_t *ui);

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
        .on_event  = camera_page_on_event,
    },
    .dsc =
    {
        .header = { .cf = LV_IMG_CF_TRUE_COLOR, .w = 0, .h = 0 },
        .data_size = 0,
        .data = NULL,
    },
};

/* 由基类指针还原派生视图 */
static inline camera_page_ui_t *camera_ui(view_t *self)
{
    return container_of(self, camera_page_ui_t, base);
}

/**********************
 *   显示
 **********************/

/* 在黑色背景上显示/更新提示文字(幂等) */
static void camera_tip_show(camera_page_ui_t *ui, const char *text)
{
    if (ui->screen == NULL)
    {
        return;
    }

    if (ui->tip == NULL)
    {
        ui->tip = lv_label_create(ui->screen);
        lv_obj_set_style_text_color(ui->tip, lv_color_white(), LV_PART_MAIN);
        lv_obj_center(ui->tip);
    }

    lv_label_set_text(ui->tip, text);
}

/* LVGL 定时器: 有新帧就切换数据源并重绘 */
static void cam_frame_timer_cb(lv_timer_t *timer)
{
    const void *frame;

    (void)timer;

    if (s_ui.img == NULL)
    {
        return;
    }

    frame = camera_stream_take_frame();
    if (frame == NULL)
    {
        return;
    }

    /* 恢复出帧: 移除之前显示的异常提示 */
    if (s_ui.tip != NULL)
    {
        lv_obj_del(s_ui.tip);
        s_ui.tip = NULL;
    }

    s_ui.dsc.data = (void *)frame;

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

/* 启动采集并开始轮询; false=摄像头不可用 */
static bool cam_display_start(camera_page_ui_t *ui)
{
    uint16_t w;
    uint16_t h;

    if (!camera_service_start())
    {
        return false;
    }

    if (camera_stream_frame_size(&w, &h))
    {
        ui->dsc.header.w  = w;
        ui->dsc.header.h  = h;
        ui->dsc.data_size = camera_stream_frame_bytes();
    }

    if (ui->timer == NULL)
    {
        ui->timer = lv_timer_create(cam_frame_timer_cb, CAM_PAGE_TIMER_MS, NULL);
    }

    return true;
}

/* 停止轮询并停采集 */
static void cam_display_stop(camera_page_ui_t *ui)
{
    if (ui->timer != NULL)
    {
        lv_timer_del(ui->timer);
        ui->timer = NULL;
    }

    camera_service_stop();
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

    if (ui->timer != NULL)
    {
        lv_timer_del(ui->timer);
        ui->timer = NULL;
    }

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

/* 首次进入: 建界面 + 启动显示/采集 */
static void camera_page_on_start(view_t *self)
{
    camera_page_ui_t *ui = camera_ui(self);

    camera_page_create(self);
    lv_scr_load(ui->screen);
    lv_obj_invalidate(ui->screen);

    if (!cam_display_start(ui))
    {
        camera_tip_show(ui, "Camera not available");
    }
}

/* 被其它页覆盖: 暂停采集, 省电也避免两次抢摄像头 */
static void camera_page_on_pause(view_t *self)
{
    cam_display_stop(camera_ui(self));
}

/* 恢复: 重新开流 */
static void camera_page_on_resume(view_t *self)
{
    camera_page_ui_t *ui = camera_ui(self);

    if (ui->screen != NULL)
    {
        lv_scr_load(ui->screen);
        lv_obj_invalidate(ui->screen);

        if (!cam_display_start(ui))
        {
            camera_tip_show(ui, "Camera not available");
        }
    }
}

/* 离开页面: 停采集并销毁控件(服务内缓冲保留复用) */
static void camera_page_on_stop(view_t *self)
{
    cam_display_stop(camera_ui(self));
    camera_page_destroy(self);
}

/* 事件分发: 采集异常 -> 显示提示(仅当前页会收到) */
static void camera_page_on_event(view_t *self, const char *event_id)
{
    camera_page_ui_t *ui = camera_ui(self);

    if (event_id == NULL || ui->screen == NULL)
    {
        return;
    }

    if (strcmp(event_id, "cam_error") == 0)
    {
        camera_tip_show(ui, "Camera error");
    }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void camera_page_init(void)
{
    page_manager_init();
    page_register(&s_ui.base);
}
