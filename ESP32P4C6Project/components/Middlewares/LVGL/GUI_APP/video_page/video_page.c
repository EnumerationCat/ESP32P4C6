/**
 * @file video_page.c
 * @brief 视频页: 扫描/User/VIDEO下的.avi文件并列表展示, 点击进入全屏播放
 *
 * 与 user_home_page 相同的 C 式继承: base 必须是第一个成员。
 * 播放本身由 BSP 的 videoplay 任务完成, 这里只负责文件列表和启动播放。
 */

#include "video_page.h"
#include "user_home_page.h"
#include "page_manager.h"
#include "view.h"
#include "container_of.h"
#include "lvgl.h"
#include "lcd.h"
#include "videoplay.h"
#include "sdmmc.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "video_page";

#define VIDEO_MAX_FILES     24      /* 列表最多展示的文件数 */
#define VIDEO_PATH_LEN      300     /* 完整路径长度上限 */

/* 派生视图: base必须是第一个成员(C式继承) + LCD界面对象 */
typedef struct
{
    view_t base;                    /* Framework基类(必须放第一位) */
    lv_obj_t *screen;               /* 视频页屏幕 */
    lv_obj_t *list;                 /* 文件列表 */
} video_page_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void video_page_create(view_t *self);
static void video_page_destroy(view_t *self);
static void video_page_on_start(view_t *self);
static void video_page_on_resume(view_t *self);
static void video_page_on_stop(view_t *self);

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
    },
};

static char s_paths[VIDEO_MAX_FILES][VIDEO_PATH_LEN];
static int s_file_count = 0;

/**********************
 *   文件列表
 **********************/

/* 由基类指针还原派生视图 */
static inline video_page_ui_t *video_ui(view_t *self)
{
    return container_of(self, video_page_ui_t, base);
}

/* 判断文件名是否以 .avi 结尾(不区分大小写) */
static bool is_avi_name(const char *name)
{
    size_t len = strlen(name);

    if (len < 5)
    {
        return false;
    }

    const char *ext = name + len - 4;

    return ext[0] == '.' &&
           tolower((unsigned char)ext[1]) == 'a' &&
           tolower((unsigned char)ext[2]) == 'v' &&
           tolower((unsigned char)ext[3]) == 'i';
}

/* 扫描 /User/VIDEO, 记录前 VIDEO_MAX_FILES 个 avi 文件路径 */
static void scan_video_files(void)
{
    DIR *dir;
    struct dirent *ent;

    s_file_count = 0;

    dir = opendir(VIDEOPLAY_DIR);
    if (dir == NULL)
    {
        ESP_LOGW(TAG, "open %s failed", VIDEOPLAY_DIR);
        return;
    }

    while ((ent = readdir(dir)) != NULL && s_file_count < VIDEO_MAX_FILES)
    {
        if (!is_avi_name(ent->d_name))
        {
            continue;
        }

        snprintf(s_paths[s_file_count], sizeof(s_paths[0]), "%s/%s", VIDEOPLAY_DIR, ent->d_name);
        s_file_count++;
    }

    closedir(dir);

    ESP_LOGI(TAG, "found %d avi file(s)", s_file_count);
}

/* 列表按钮点击: 启动播放 */
static void video_btn_event_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);

    if (index < 0 || index >= s_file_count)
    {
        return;
    }

    esp_err_t ret = videoplay_play(s_paths[index]);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "play %s failed: %s", s_paths[index], esp_err_to_name(ret));
    }
}

/* 返回主界面 */
static void back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    manager_gopage(USER_HOME_PAGE_ID);
}

/**********************
 *   页面生命周期
 **********************/

/* 创建视频页控件(幂等) */
static void video_page_create(view_t *self)
{
    video_page_ui_t *ui = video_ui(self);

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

    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* 文件列表 */
    ui->list = lv_list_create(ui->screen);
    lv_obj_set_size(ui->list, lcddev.width - 16, lcddev.height - 80);
    lv_obj_align(ui->list, LV_ALIGN_TOP_MID, 0, 56);

    if (sdmmc_mount_state != SDMMC_MOUNT_STATE_MOUNTED)
    {
        lv_list_add_text(ui->list, "SD card not mounted");
        return;
    }

    scan_video_files();

    if (s_file_count == 0)
    {
        lv_list_add_text(ui->list, "No .avi in /VIDEO");
        return;
    }

    for (int i = 0; i < s_file_count; i++)
    {
        const char *name = strrchr(s_paths[i], '/');

        name = (name != NULL) ? name + 1 : s_paths[i];

        lv_obj_t *btn = lv_list_add_btn(ui->list, NULL, name);
        lv_obj_add_event_cb(btn, video_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
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

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void video_page_init(void)
{
    page_manager_init();
    page_register(&s_ui.base);
}
