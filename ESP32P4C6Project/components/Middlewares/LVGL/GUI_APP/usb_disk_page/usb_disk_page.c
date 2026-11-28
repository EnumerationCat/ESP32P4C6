/**
 * @file usb_disk_page.c
 * @brief U盘模式页: 一键把SD卡切换成PC可读写的U盘
 *
 * 与 user_home_page 相同的 C 式继承: base 必须是第一个成员。
 * 模式切换由 BSP 的 usb_msc_device 完成, 本页只负责提示和按钮。
 */

#include "usb_disk_page.h"
#include "user_home_page.h"
#include "page_manager.h"
#include "view.h"
#include "view_bind.h"
#include "container_of.h"
#include "lvgl.h"
#include "sdmmc.h"
#include "usb_msc_device.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "usb_disk_page";

/* 派生视图: base必须是第一个成员(C式继承) + LCD界面对象 */
typedef struct
{
    view_t base;                /* Framework基类(必须放第一位) */
    lv_obj_t *screen;           /* 页面屏幕 */
    lv_obj_t *status;           /* 状态提示 */
    lv_obj_t *toggle_btn;       /* 进入/退出U盘模式 */
    lv_obj_t *toggle_label;     /* 按钮文字 */
} usb_disk_page_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void usb_disk_page_create(view_t *self);
static void usb_disk_page_destroy(view_t *self);
static void usb_disk_page_on_start(view_t *self);
static void usb_disk_page_on_resume(view_t *self);
static void usb_disk_page_on_stop(view_t *self);
static void usb_disk_page_on_click(view_t *self, const char *click_id, intptr_t user_data);

static usb_disk_page_ui_t s_ui =
{
    .base =
    {
        .id        = USB_DISK_PAGE_ID,
        .name      = "usb_disk",
        .init      = usb_disk_page_create,
        .deinit    = usb_disk_page_destroy,
        .on_start  = usb_disk_page_on_start,
        .on_resume = usb_disk_page_on_resume,
        .on_stop   = usb_disk_page_on_stop,
        .on_click  = usb_disk_page_on_click,
    },
};

/**********************
 *   界面刷新
 **********************/

/* 由基类指针还原派生视图 */
static inline usb_disk_page_ui_t *usb_disk_ui(view_t *self)
{
    return container_of(self, usb_disk_page_ui_t, base);
}

/* 根据当前模式更新提示文字 */
static void usb_disk_refresh(usb_disk_page_ui_t *ui)
{
    if (usb_msc_device_active())
    {
        lv_label_set_text(ui->status,
                          "U盘模式已开启\n\n"
                          "用 A-A 数据线把板子接到电脑,\n"
                          "电脑上会出现SD卡磁盘。\n\n"
                          "在电脑上安全弹出磁盘后,\n"
                          "再点下面的按钮退出。");
        lv_label_set_text(ui->toggle_label, "退出U盘模式");
    }
    else if (sdmmc_mount_state == SDMMC_MOUNT_STATE_MOUNTED)
    {
        lv_label_set_text(ui->status,
                          "未进入U盘模式\n\n"
                          "进入后USB鼠标会暂时不可用,\n"
                          "SD卡可在电脑上读写。");
        lv_label_set_text(ui->toggle_label, "进入U盘模式");
    }
    else
    {
        lv_label_set_text(ui->status,
                          "SD卡未挂载\n\n"
                          "请检查SD卡后重启设备。");
        lv_label_set_text(ui->toggle_label, "进入U盘模式");
    }
}

/* 进入/退出U盘模式 */
static void usb_disk_toggle(view_t *self)
{
    usb_disk_page_ui_t *ui = usb_disk_ui(self);

    if (!usb_msc_device_active())
    {
        esp_err_t ret = usb_msc_device_start();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "enter U-disk mode failed: %s", esp_err_to_name(ret));
            lv_label_set_text(ui->status, "进入U盘模式失败\n详情见串口日志");
            return;
        }
    }
    else
    {
        esp_err_t ret = usb_msc_device_stop();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "exit U-disk mode: %s", esp_err_to_name(ret));
        }
    }

    usb_disk_refresh(ui);
}

/* 点击分发 */
static void usb_disk_page_on_click(view_t *self, const char *click_id, intptr_t user_data)
{
    (void)user_data;

    if (click_id == NULL)
    {
        return;
    }

    if (strcmp(click_id, "toggle") == 0)
    {
        usb_disk_toggle(self);
    }
    else if (strcmp(click_id, "back") == 0)
    {
        /* 返回主界面(如果还在U盘模式先退出) */
        if (usb_msc_device_active())
        {
            usb_msc_device_stop();
        }

        manager_gopage(USER_HOME_PAGE_ID);
    }
}

/**********************
 *   页面生命周期
 **********************/

/* 创建页面控件(幂等) */
static void usb_disk_page_create(view_t *self)
{
    usb_disk_page_ui_t *ui = usb_disk_ui(self);

    if (ui->screen != NULL)
    {
        return;
    }

    ui->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui->screen, lv_color_black(), LV_PART_MAIN);

    /* 标题 */
    lv_obj_t *title = lv_label_create(ui->screen);
    lv_label_set_text(title, "USB DISK");
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

    /* 状态提示 */
    ui->status = lv_label_create(ui->screen);
    lv_obj_set_width(ui->status, lv_pct(90));
    lv_label_set_long_mode(ui->status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(ui->status, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(ui->status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(ui->status, LV_ALIGN_CENTER, 0, -30);

    /* 模式切换按钮 */
    ui->toggle_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(ui->toggle_btn, 180, 56);
    lv_obj_align(ui->toggle_btn, LV_ALIGN_BOTTOM_MID, 0, -36);

    ui->toggle_label = lv_label_create(ui->toggle_btn);
    lv_obj_center(ui->toggle_label);

    view_bind_click(ui->toggle_btn, "toggle", 0);

    usb_disk_refresh(ui);
}

/* 销毁页面控件 */
static void usb_disk_page_destroy(view_t *self)
{
    usb_disk_page_ui_t *ui = usb_disk_ui(self);

    if (ui->screen == NULL)
    {
        return;
    }

    lv_obj_del(ui->screen);
    ui->screen = NULL;
    ui->status = NULL;
    ui->toggle_btn = NULL;
    ui->toggle_label = NULL;
}

/* 首次进入 */
static void usb_disk_page_on_start(view_t *self)
{
    usb_disk_page_ui_t *ui = usb_disk_ui(self);

    usb_disk_page_create(self);
    usb_disk_refresh(ui);
    lv_scr_load(ui->screen);
    lv_obj_invalidate(ui->screen);
}

/* 从挂起恢复 */
static void usb_disk_page_on_resume(view_t *self)
{
    usb_disk_page_ui_t *ui = usb_disk_ui(self);

    if (ui->screen != NULL)
    {
        usb_disk_refresh(ui);
        lv_scr_load(ui->screen);
        lv_obj_invalidate(ui->screen);
    }
}

/* 离开页面: 必须退出U盘模式并恢复SD卡和鼠标 */
static void usb_disk_page_on_stop(view_t *self)
{
    if (usb_msc_device_active())
    {
        usb_msc_device_stop();
    }

    usb_disk_page_destroy(self);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void usb_disk_page_init(void)
{
    page_manager_init();
    page_register(&s_ui.base);
}
