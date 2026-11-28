/**
 * @file recorder_play_page.c
 * @brief 录音回放页: 扫描/User/RECORDER下的WAV并列表播放
 *
 * 与 video_page 相同的 C 式继承: base 必须是第一个成员。
 * 文件扫描/缓存在 Framework/Model/recorder_list_model; 播放本身由 BSP 的
 * recorder 播放任务完成, 这里只渲染列表、发起播放命令并订阅 "play_done"。
 */

#include "recorder_play_page.h"
#include "recorder_page.h"
#include "page_manager.h"
#include "view.h"
#include "view_bind.h"
#include "container_of.h"
#include "lvgl.h"
#include "lcd.h"
#include "recorder.h"
#include "sdmmc.h"
#include "file_list_model.h"

#include <string.h>

#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "recorder_play";

#define PLAY_DIR    MOUNT_POINT "/RECORDER"     /* "/User/RECORDER" */

/* 派生视图: base必须是第一个成员(C式继承) + LCD界面对象 */
typedef struct
{
    view_t base;                /* Framework基类(必须放第一位) */
    lv_obj_t *screen;           /* 回放页屏幕 */
    lv_obj_t *list;             /* 文件列表 */
    lv_obj_t *status_label;     /* Playing / Stopped / Play failed */
    lv_obj_t *stop_btn;         /* 停止播放 */

    bool last_playing;          /* 上次显示的播放状态 */
    int64_t last_update_us;     /* 上次刷新时间(节流) */
} recorder_play_page_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void play_page_create(view_t *self);
static void play_page_destroy(view_t *self);
static void play_page_on_start(view_t *self);
static void play_page_on_resume(view_t *self);
static void play_page_on_pause(view_t *self);
static void play_page_on_stop(view_t *self);
static void play_page_on_update(view_t *self);
static void play_page_on_click(view_t *self, const char *click_id, intptr_t user_data);
static void play_page_on_event(view_t *self, const char *event_id, intptr_t user_data);

/* .wav 路径缓存; 单独静态存储(未初始化 -> .bss), 不随 s_ui 的初始化进 flash */
static file_list_t s_files;

static recorder_play_page_ui_t s_ui =
{
    .base =
    {
        .id        = RECORDER_PLAY_PAGE_ID,
        .name      = "recorder_play",
        .init      = play_page_create,
        .deinit    = play_page_destroy,
        .on_start  = play_page_on_start,
        .on_resume = play_page_on_resume,
        .on_pause  = play_page_on_pause,
        .on_stop   = play_page_on_stop,
        .on_update = play_page_on_update,
        .on_click  = play_page_on_click,
        .on_event  = play_page_on_event,
    },
};

/**********************
 *   文件列表
 **********************/

/* 由基类指针还原派生视图 */
static inline recorder_play_page_ui_t *play_ui(view_t *self)
{
    return container_of(self, recorder_play_page_ui_t, base);
}

/* 清空并重新填充列表(每次进入/恢复都重新扫描, 以便看到新录音) */
static void populate_list(recorder_play_page_ui_t *ui)
{
    int i;
    int count;

    if (ui->list == NULL)
    {
        return;
    }

    lv_obj_clean(ui->list);

    if (sdmmc_mount_state != SDMMC_MOUNT_STATE_MOUNTED)
    {
        lv_list_add_text(ui->list, "SD card not mounted");
        return;
    }

    file_list_scan(&s_files, PLAY_DIR, "wav");
    count = file_list_count(&s_files);

    if (count == 0)
    {
        lv_list_add_text(ui->list, "No recordings");
        return;
    }

    for (i = 0; i < count; i++)
    {
        lv_obj_t *btn = lv_list_add_btn(ui->list, NULL, file_list_name(&s_files, i));
        view_bind_click(btn, "file", i);
    }
}

/**********************
 *   按钮点击
 **********************/

/* 点击文件: 停止当前播放并播放所选文件 */
static void play_file(int index)
{
    const char *path = file_list_path(&s_files, index);
    esp_err_t ret;

    if (path == NULL)
    {
        return;
    }

    if (recorder_is_playing())
    {
        recorder_play_stop();
    }

    ret = recorder_play(path);

    if (ret == ESP_OK)
    {
        lv_label_set_text_fmt(s_ui.status_label, "Playing: %s",
                              file_list_name(&s_files, index));
        lv_obj_clear_state(s_ui.stop_btn, LV_STATE_DISABLED);
        s_ui.last_playing = true;
    }
    else
    {
        lv_label_set_text(s_ui.status_label, "Play failed");
        lv_obj_add_state(s_ui.stop_btn, LV_STATE_DISABLED);
        s_ui.last_playing = false;
    }
}

/* 停止播放 */
static void play_stop(void)
{
    recorder_play_stop();
    lv_label_set_text(s_ui.status_label, "Stopped");
    lv_obj_add_state(s_ui.stop_btn, LV_STATE_DISABLED);
    s_ui.last_playing = false;
}

/* 点击分发 */
static void play_page_on_click(view_t *self, const char *click_id, intptr_t user_data)
{
    (void)self;

    if (click_id == NULL)
    {
        return;
    }

    if (strcmp(click_id, "file") == 0)
    {
        play_file((int)user_data);
    }
    else if (strcmp(click_id, "stop") == 0)
    {
        play_stop();
    }
    else if (strcmp(click_id, "back") == 0)
    {
        /* 切页触发 on_pause, 播放随之停止 */
        manager_gopage(RECORDER_PAGE_ID);
    }
}

/**********************
 *   页面生命周期
 **********************/

/* 创建回放页控件(幂等) */
static void play_page_create(view_t *self)
{
    recorder_play_page_ui_t *ui = play_ui(self);
    lv_obj_t *title;
    lv_obj_t *back_btn;
    lv_obj_t *back_label;
    lv_obj_t *stop_label;

    if (ui->screen != NULL)
    {
        return;
    }

    ui->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui->screen, lv_color_black(), LV_PART_MAIN);

    /* 标题 */
    title = lv_label_create(ui->screen);
    lv_label_set_text(title, "RECORDINGS");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 返回按钮 */
    back_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(back_btn, 64, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 8, 8);

    back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    view_bind_click(back_btn, "back", 0);

    /* 状态 */
    ui->status_label = lv_label_create(ui->screen);
    lv_label_set_text(ui->status_label, "Tap a file to play");
    lv_obj_set_style_text_color(ui->status_label, lv_color_make(0xB0, 0xB0, 0xB0), LV_PART_MAIN);
    lv_label_set_long_mode(ui->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui->status_label, 280);
    lv_obj_set_style_text_align(ui->status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(ui->status_label, LV_ALIGN_TOP_MID, 0, 44);

    /* 文件列表 */
    ui->list = lv_list_create(ui->screen);
    lv_obj_set_size(ui->list, lcddev.width - 16, lcddev.height - 150);
    lv_obj_align(ui->list, LV_ALIGN_TOP_MID, 0, 78);

    /* 停止按钮 */
    ui->stop_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(ui->stop_btn, 140, 48);
    lv_obj_align(ui->stop_btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_state(ui->stop_btn, LV_STATE_DISABLED);      /* 初始未播放 */
    view_bind_click(ui->stop_btn, "stop", 0);

    stop_label = lv_label_create(ui->stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_center(stop_label);
}

/* 销毁回放页控件 */
static void play_page_destroy(view_t *self)
{
    recorder_play_page_ui_t *ui = play_ui(self);

    if (ui->screen == NULL)
    {
        return;
    }

    lv_obj_del(ui->screen);
    ui->screen       = NULL;
    ui->list         = NULL;
    ui->status_label = NULL;
    ui->stop_btn     = NULL;
}

/* 首次进入 */
static void play_page_on_start(view_t *self)
{
    recorder_play_page_ui_t *ui = play_ui(self);

    play_page_create(self);
    populate_list(ui);
    s_ui.last_playing = recorder_is_playing();
    lv_scr_load(ui->screen);
    lv_obj_invalidate(ui->screen);
}

/* 从挂起恢复: 重新扫描目录, 显示最新录音 */
static void play_page_on_resume(view_t *self)
{
    recorder_play_page_ui_t *ui = play_ui(self);

    if (ui->screen != NULL)
    {
        populate_list(ui);
        s_ui.last_playing = recorder_is_playing();
        lv_scr_load(ui->screen);
        lv_obj_invalidate(ui->screen);
    }
}

/* 离开页面: 停止播放(page_manager 切页只调用 on_pause) */
static void play_page_on_pause(view_t *self)
{
    if (recorder_is_playing())
    {
        if (recorder_play_stop() != ESP_OK)
        {
            ESP_LOGW(TAG, "stop on pause failed");
        }
    }

    if (s_ui.status_label != NULL)
    {
        lv_label_set_text(s_ui.status_label, "Stopped");
    }

    if (s_ui.stop_btn != NULL)
    {
        lv_obj_add_state(s_ui.stop_btn, LV_STATE_DISABLED);
    }

    s_ui.last_playing = false;
    (void)self;
}

/* 停止页面 */
static void play_page_on_stop(view_t *self)
{
    if (recorder_is_playing())
    {
        recorder_play_stop();
    }

    play_page_destroy(self);
}

/* 周期刷新: 播放自然结束后恢复按钮状态, 250ms 节流 */
static void play_page_on_update(view_t *self)
{
    int64_t now = esp_timer_get_time();
    bool playing;

    (void)self;

    if (now - s_ui.last_update_us < 250 * 1000)
    {
        return;
    }

    s_ui.last_update_us = now;

    if (s_ui.screen == NULL)
    {
        return;
    }

    playing = recorder_is_playing();

    if (playing != s_ui.last_playing)
    {
        s_ui.last_playing = playing;

        if (playing)
        {
            lv_obj_clear_state(s_ui.stop_btn, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_state(s_ui.stop_btn, LV_STATE_DISABLED);
            lv_label_set_text(s_ui.status_label, "Stopped");
        }
    }
}

/* 事件分发: 播放自然结束 -> 恢复停止按钮/状态(仅当前页会收到) */
static void play_page_on_event(view_t *self, const char *event_id, intptr_t user_data)
{
    (void)self;
    (void)user_data;

    if (event_id == NULL || strcmp(event_id, "play_done") != 0)
    {
        return;
    }

    if (recorder_is_playing())
    {
        return;             /* 已被新的播放接管, 忽略旧的结束事件 */
    }

    s_ui.last_playing = false;

    if (s_ui.stop_btn != NULL)
    {
        lv_obj_add_state(s_ui.stop_btn, LV_STATE_DISABLED);
    }

    if (s_ui.status_label != NULL)
    {
        lv_label_set_text(s_ui.status_label, "Stopped");
    }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void recorder_play_page_init(void)
{
    page_manager_init();
    page_register(&s_ui.base);
}
