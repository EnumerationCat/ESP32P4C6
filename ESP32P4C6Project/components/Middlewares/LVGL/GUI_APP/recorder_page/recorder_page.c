/**
 * @file recorder_page.c
 * @brief 录音页: 触摸按钮开始/停止录音, 显示时长与文件名
 *
 * 与 video_page 相同的 C 式继承: base 必须是第一个成员。
 * 录音本身由 BSP 的 recorder 任务完成, 这里只负责界面与调用。
 *
 * 注意: page_manager 切页只调用 on_pause(不调用 on_stop),
 *       所以 on_pause 和 on_stop 都要停止录音, 否则离开页面会一直录下去。
 */

#include "recorder_page.h"
#include "recorder_play_page.h"
#include "user_home_page.h"
#include "page_manager.h"
#include "view.h"
#include "view_bind.h"
#include "container_of.h"
#include "lvgl.h"
#include "recorder.h"
#include "sdmmc.h"

#include <string.h>

#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "recorder_page";

/* 派生视图: base必须是第一个成员(C式继承) + LCD界面对象 */
typedef struct
{
    view_t base;                    /* Framework基类(必须放第一位) */
    lv_obj_t *screen;               /* 录音页屏幕 */
    lv_obj_t *time_label;           /* 录音时长 00:00 */
    lv_obj_t *status_label;         /* Ready / Recording... / Saved */
    lv_obj_t *file_label;           /* 当前/最近一次文件名 */
    lv_obj_t *rec_btn;              /* 开始录音 */
    lv_obj_t *stop_btn;             /* 停止并保存 */

    uint32_t last_sec;              /* 上次显示的秒数 */
    bool last_rec;                  /* 上次显示的录音状态 */
    int64_t last_update_us;         /* 上次刷新时间(节流) */
} recorder_page_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void recorder_page_create(view_t *self);
static void recorder_page_destroy(view_t *self);
static void recorder_page_on_start(view_t *self);
static void recorder_page_on_resume(view_t *self);
static void recorder_page_on_pause(view_t *self);
static void recorder_page_on_stop(view_t *self);
static void recorder_page_on_update(view_t *self);
static void recorder_page_on_click(view_t *self, const char *click_id, intptr_t arg);
static void recorder_page_on_event(view_t *self, const char *event_id);

static recorder_page_ui_t s_ui =
{
    .base =
    {
        .id        = RECORDER_PAGE_ID,
        .name      = "recorder",
        .init      = recorder_page_create,
        .deinit    = recorder_page_destroy,
        .on_start  = recorder_page_on_start,
        .on_resume = recorder_page_on_resume,
        .on_pause  = recorder_page_on_pause,
        .on_stop   = recorder_page_on_stop,
        .on_update = recorder_page_on_update,
        .on_click  = recorder_page_on_click,
        .on_event  = recorder_page_on_event,
    },
};

/**********************
 *   内部辅助
 **********************/

/* 由基类指针还原派生视图 */
static inline recorder_page_ui_t *recorder_ui(view_t *self)
{
    return container_of(self, recorder_page_ui_t, base);
}

/* 刷新时长、状态与按钮禁用状态; force=true 时无视缓存全部刷新 */
static void refresh_ui(view_t *self, bool force)
{
    recorder_page_ui_t *ui = recorder_ui(self);
    uint32_t sec;
    bool rec;
    bool sd_ok;
    const char *name;

    if (ui->screen == NULL || ui->time_label == NULL)
    {
        return;
    }

    sec   = recorder_elapsed_sec();
    rec   = recorder_is_recording();
    sd_ok = (sdmmc_mount_state == SDMMC_MOUNT_STATE_MOUNTED);

    if (force || sec != ui->last_sec)
    {
        ui->last_sec = sec;
        lv_label_set_text_fmt(ui->time_label, "%02u:%02u",
                              (unsigned)(sec / 60), (unsigned)(sec % 60));
    }

    if (force || rec != ui->last_rec)
    {
        ui->last_rec = rec;

        if (rec)
        {
            lv_obj_add_state(ui->rec_btn, LV_STATE_DISABLED);
            lv_obj_clear_state(ui->stop_btn, LV_STATE_DISABLED);
            lv_label_set_text(ui->status_label, "Recording...");
        }
        else
        {
            lv_obj_clear_state(ui->rec_btn, LV_STATE_DISABLED);
            lv_obj_add_state(ui->stop_btn, LV_STATE_DISABLED);

            if (!sd_ok)
            {
                lv_label_set_text(ui->status_label, "SD not ready");
            }
            else if (sec > 0)
            {
                lv_label_set_text(ui->status_label, "Saved");
            }
            else
            {
                lv_label_set_text(ui->status_label, "Ready");
            }
        }

        name = recorder_current_file_name();
        lv_label_set_text(ui->file_label,
                          (name != NULL && name[0] != '\0') ? name : "-");
    }
}

/**********************
 *   按钮点击
 **********************/

/* 开始录音 */
static void rec_start(view_t *self)
{
    recorder_page_ui_t *ui = recorder_ui(self);
    esp_err_t ret;

    if (recorder_is_recording())
    {
        return;
    }

    ret = recorder_start();
    if (ret != ESP_OK)
    {
        if (sdmmc_mount_state != SDMMC_MOUNT_STATE_MOUNTED)
        {
            lv_label_set_text(ui->status_label, "SD not ready");
        }
        else
        {
            lv_label_set_text(ui->status_label, "Start failed");
        }
        return;
    }

    refresh_ui(self, true);
}

/* 停止并保存 */
static void rec_stop(view_t *self)
{
    if (recorder_stop() != ESP_OK)
    {
        ESP_LOGW(TAG, "stop recording failed");
    }

    refresh_ui(self, true);
}

/* 点击分发 */
static void recorder_page_on_click(view_t *self, const char *click_id, intptr_t arg)
{
    (void)arg;

    if (click_id == NULL)
    {
        return;
    }

    if (strcmp(click_id, "rec") == 0)
    {
        rec_start(self);
    }
    else if (strcmp(click_id, "stop") == 0)
    {
        rec_stop(self);
    }
    else if (strcmp(click_id, "list") == 0)
    {
        /* 切页触发 on_pause, 正在录音会先停止保存 */
        manager_gopage(RECORDER_PLAY_PAGE_ID);
    }
    else if (strcmp(click_id, "back") == 0)
    {
        /* 切页会触发 on_pause, 录音随之停止保存 */
        manager_gopage(USER_HOME_PAGE_ID);
    }
}

/**********************
 *   页面生命周期
 **********************/

/* 创建录音页控件(幂等) */
static void recorder_page_create(view_t *self)
{
    recorder_page_ui_t *ui = recorder_ui(self);
    lv_obj_t *title;
    lv_obj_t *back_btn;
    lv_obj_t *back_label;
    lv_obj_t *hint;
    lv_obj_t *label;
    lv_obj_t *list_btn;
    lv_obj_t *list_label;

    if (ui->screen != NULL)
    {
        return;
    }

    ui->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui->screen, lv_color_black(), LV_PART_MAIN);

    /* 标题 */
    title = lv_label_create(ui->screen);
    lv_label_set_text(title, "RECORDER");
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

    /* 录音回放列表按钮(右上角) */
    list_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(list_btn, 64, 36);
    lv_obj_align(list_btn, LV_ALIGN_TOP_RIGHT, -8, 8);

    list_label = lv_label_create(list_btn);
    lv_label_set_text(list_label, "LIST");
    lv_obj_center(list_label);

    view_bind_click(list_btn, "list", 0);

    /* 录音时长 */
    ui->time_label = lv_label_create(ui->screen);
    lv_label_set_text(ui->time_label, "00:00");
    lv_obj_set_style_text_color(ui->time_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->time_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(ui->time_label, LV_ALIGN_TOP_MID, 0, 60);

    /* 状态 */
    ui->status_label = lv_label_create(ui->screen);
    lv_label_set_text(ui->status_label, "Ready");
    lv_obj_set_style_text_color(ui->status_label, lv_color_make(0xB0, 0xB0, 0xB0), LV_PART_MAIN);
    lv_obj_align(ui->status_label, LV_ALIGN_TOP_MID, 0, 108);

    /* 开始录音按钮 */
    ui->rec_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(ui->rec_btn, 160, 56);
    lv_obj_align(ui->rec_btn, LV_ALIGN_TOP_MID, 0, 150);
    view_bind_click(ui->rec_btn, "rec", 0);

    label = lv_label_create(ui->rec_btn);
    lv_label_set_text(label, "REC");
    lv_obj_center(label);

    /* 停止并保存按钮 */
    ui->stop_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(ui->stop_btn, 160, 56);
    lv_obj_align(ui->stop_btn, LV_ALIGN_TOP_MID, 0, 222);
    view_bind_click(ui->stop_btn, "stop", 0);

    label = lv_label_create(ui->stop_btn);
    lv_label_set_text(label, "STOP&SAVE");
    lv_obj_center(label);

    /* 文件名 */
    ui->file_label = lv_label_create(ui->screen);
    lv_label_set_text(ui->file_label, "-");
    lv_obj_set_style_text_color(ui->file_label, lv_color_make(0x80, 0xC0, 0xFF), LV_PART_MAIN);
    lv_label_set_long_mode(ui->file_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui->file_label, 300);
    lv_obj_set_style_text_align(ui->file_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(ui->file_label, LV_ALIGN_TOP_MID, 0, 300);

    /* 底部提示 */
    hint = lv_label_create(ui->screen);
    lv_label_set_text(hint, "SD: /User/RECORDER");
    lv_obj_set_style_text_color(hint, lv_color_make(0x80, 0x80, 0x80), LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);
}

/* 销毁录音页控件 */
static void recorder_page_destroy(view_t *self)
{
    recorder_page_ui_t *ui = recorder_ui(self);

    if (ui->screen == NULL)
    {
        return;
    }

    lv_obj_del(ui->screen);
    ui->screen       = NULL;
    ui->time_label   = NULL;
    ui->status_label = NULL;
    ui->file_label   = NULL;
    ui->rec_btn      = NULL;
    ui->stop_btn     = NULL;
}

/* 首次进入 */
static void recorder_page_on_start(view_t *self)
{
    recorder_page_ui_t *ui = recorder_ui(self);

    recorder_page_create(self);
    refresh_ui(self, true);
    lv_scr_load(ui->screen);
    lv_obj_invalidate(ui->screen);
}

/* 从挂起恢复(资源保留) */
static void recorder_page_on_resume(view_t *self)
{
    recorder_page_ui_t *ui = recorder_ui(self);

    if (ui->screen != NULL)
    {
        refresh_ui(self, true);
        lv_scr_load(ui->screen);
        lv_obj_invalidate(ui->screen);
    }
}

/* 离开页面: 必须停止录音(page_manager 切页只调用 on_pause) */
static void recorder_page_on_pause(view_t *self)
{
    if (recorder_is_recording())
    {
        if (recorder_stop() != ESP_OK)
        {
            ESP_LOGW(TAG, "stop on pause failed");
        }
    }

    refresh_ui(self, true);
}

/* 停止页面 */
static void recorder_page_on_stop(view_t *self)
{
    if (recorder_is_recording())
    {
        recorder_stop();
    }

    recorder_page_destroy(self);
}

/* 周期刷新(仅当前可见页会被调用), 200ms 节流 */
static void recorder_page_on_update(view_t *self)
{
    recorder_page_ui_t *ui = recorder_ui(self);
    int64_t now = esp_timer_get_time();

    if (now - ui->last_update_us < 200 * 1000)
    {
        return;
    }

    ui->last_update_us = now;
    refresh_ui(self, false);
}

/* 事件分发: 录音任务结束 -> 立即刷新状态/文件名(仅当前页会收到) */
static void recorder_page_on_event(view_t *self, const char *event_id)
{
    if (event_id != NULL && strcmp(event_id, "rec_done") == 0)
    {
        refresh_ui(self, true);
    }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void recorder_page_init(void)
{
    page_manager_init();
    page_register(&s_ui.base);
}
