/**
 * @file video_page.c
 * @brief 视频页: 展示 /User/VIDEO 下的 .avi 列表, 点击进入全屏播放
 *
 * 与其它页面相同的 C 式继承: base 必须是第一个成员。
 *
 * 分层:
 *  - 文件扫描/路径缓存在 Framework/Model/video_list_model;
 *  - 播放结束的事件桥接在 Framework/Service/video_event_service;
 *  - 本页只渲染列表、发起播放命令, 并在 on_event 里订阅 "video_done"。
 */

#include "video_page.h"
#include "user_home_page.h"
#include "page_manager.h"
#include "view.h"
#include "view_bind.h"
#include "container_of.h"
#include "lvgl.h"
#include "lcd.h"
#include "videoplay.h"
#include "sdmmc.h"
#include "file_list_model.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "video_page";

/* 派生视图: base必须是第一个成员(C式继承) + LCD界面对象 */
typedef struct
{
    view_t base;                    /* Framework基类(必须放第一位) */
    lv_obj_t *screen;               /* 视频页屏幕 */
    lv_obj_t *list;                 /* 文件列表控件 */
} video_page_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void video_page_create(view_t *self);
static void video_page_destroy(view_t *self);
static void video_page_on_start(view_t *self);
static void video_page_on_resume(view_t *self);
static void video_page_on_stop(view_t *self);
static void video_page_on_click(view_t *self, const char *click_id, intptr_t arg);
static void video_page_on_event(view_t *self, const char *event_id);

/* .avi 路径缓存; 单独静态存储(未初始化 -> .bss), 不随 s_ui 的初始化进 flash */
static file_list_t s_files;

static video_page_ui_t s_ui =
{
    .base =
    {
        .id        = VIDEO_PAGE_ID,
        .name      = "video",
        .init      = video_page_create,
        .deinit    = video_page_destroy,
        .on_start  = video_page_on_start,
        .on_resume = video_page_on_resume,
        .on_stop   = video_page_on_stop,
        .on_click  = video_page_on_click,
        .on_event  = video_page_on_event,
    },
};

/**********************
 *   内部辅助
 **********************/

/* 由基类指针还原派生视图 */
static inline video_page_ui_t *video_ui(view_t *self)
{
    return container_of(self, video_page_ui_t, base);
}

/**********************
 *   页面生命周期
 **********************/

/* 创建视频页控件(幂等), 每次创建都重新扫描目录 */
static void video_page_create(view_t *self)
{
    video_page_ui_t *ui = video_ui(self);
    int count;

    if (ui->screen != NULL)
    {
        return;
    }

    ui->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui->screen, lv_color_black(), LV_PART_MAIN);

    /* 标题 */
    lv_obj_t *title = lv_label_create(ui->screen);
    lv_label_set_text(title, "VIDEO");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 返回按钮 */
    lv_obj_t *back_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(back_btn, 64, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 8, 8);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    view_bind_click(back_btn, "back", 0);

    /* 文件列表 */
    ui->list = lv_list_create(ui->screen);
    lv_obj_set_size(ui->list, lcddev.width - 16, lcddev.height - 80);
    lv_obj_align(ui->list, LV_ALIGN_TOP_MID, 0, 56);

    if (sdmmc_mount_state != SDMMC_MOUNT_STATE_MOUNTED)
    {
        lv_list_add_text(ui->list, "SD card not mounted");
        return;
    }

    file_list_scan(&s_files, VIDEOPLAY_DIR, "avi");
    count = file_list_count(&s_files);

    if (count == 0)
    {
        lv_list_add_text(ui->list, "No .avi in /VIDEO");
        return;
    }

    for (int i = 0; i < count; i++)
    {
        lv_obj_t *btn = lv_list_add_btn(ui->list, NULL, file_list_name(&s_files, i));
        view_bind_click(btn, "file", i);
    }
}

/* 销毁视频页控件 */
static void video_page_destroy(view_t *self)
{
    video_page_ui_t *ui = video_ui(self);

    if (ui->screen == NULL)
    {
        return;
    }

    lv_obj_del(ui->screen);
    ui->screen = NULL;
    ui->list = NULL;
}

/* 首次进入 */
static void video_page_on_start(view_t *self)
{
    video_page_ui_t *ui = video_ui(self);

    video_page_create(self);
    lv_scr_load(ui->screen);
    lv_obj_invalidate(ui->screen);
}

/* 从挂起恢复 */
static void video_page_on_resume(view_t *self)
{
    video_page_ui_t *ui = video_ui(self);

    if (ui->screen != NULL)
    {
        lv_scr_load(ui->screen);
        lv_obj_invalidate(ui->screen);
    }
}

/* 离开页面时销毁, 下次进入重新扫描目录 */
static void video_page_on_stop(view_t *self)
{
    video_page_destroy(self);
}

/* 点击分发: 返回 / 列表行播放 */
static void video_page_on_click(view_t *self, const char *click_id, intptr_t arg)
{
    (void)self;

    if (click_id == NULL)
    {
        return;
    }

    if (strcmp(click_id, "back") == 0)
    {
        manager_gopage(USER_HOME_PAGE_ID);
        return;
    }

    if (strcmp(click_id, "file") == 0)
    {
        const char *path = file_list_path(&s_files, (int)arg);

        if (path == NULL)
        {
            return;
        }

        esp_err_t ret = videoplay_play(path);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "play %s failed: %s", path, esp_err_to_name(ret));
        }
    }
}

/* 事件分发: 播放结束 - 播放器独占过 LCD, 整屏重绘找回界面(仅当前页会收到) */
static void video_page_on_event(view_t *self, const char *event_id)
{
    video_page_ui_t *ui = video_ui(self);

    if (event_id == NULL || ui->screen == NULL)
    {
        return;
    }

    if (strcmp(event_id, "video_done") == 0)
    {
        lv_obj_invalidate(ui->screen);
    }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void video_page_init(void)
{
    page_manager_init();
    page_register(&s_ui.base);
}
