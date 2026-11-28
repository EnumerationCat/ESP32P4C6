/**
 * @file user_home_page.c
 * @brief 主界面页面: 控件创建 + 生命周期回调
 *
 * 与 shell_page 相同的C式继承: base必须是第一个成员, 用 container_of 还原派生视图。
 * 本文件只在LVGL任务中访问控件。
 */

#include "user_home_page.h"
#include "shell_page.h"
#include "video_page.h"
#include "usb_disk_page.h"
#include "recorder_page.h"
#include "wifi_page.h"
#include "popup_notice.h"
#include "popup_confirm.h"
#include "popup_toast.h"
#include "page_manager.h"
#include "view.h"
#include "view_bind.h"
#include "container_of.h"
#include "lvgl.h"

#include <string.h>

/**********************
 *      TYPEDEFS
 **********************/

/* 派生视图: base必须是第一个成员(C式继承) + LCD界面对象 */
typedef struct
{
    view_t base;                            /* Framework基类(必须放第一位) */
    lv_obj_t *screen;                       /* 主界面屏幕 */
} user_home_page_ui_t;

/**********************
 *    STATIC PROTOTYPES
 **********************/
static void home_init(view_t *self);
static void home_deinit(view_t *self);
static void home_on_start(view_t *self);
static void home_on_resume(view_t *self);
static void home_on_stop(view_t *self);
static void home_on_click(view_t *self, const char *click_id, intptr_t arg);

/**********************
 *   STATIC VARIABLES
 **********************/
static user_home_page_ui_t s_ui =
{
    .base =
    {
        .id        = USER_HOME_PAGE_ID,
        .name      = "home",
        .init      = home_init,
        .deinit    = home_deinit,
        .on_start  = home_on_start,
        .on_resume = home_on_resume,
        .on_stop   = home_on_stop,
        .on_click  = home_on_click,
    },
};

static user_home_page_camera_cb_t s_camera_cb = NULL;

/**********************
 *   LVGL 控件操作
 **********************/

/* 由基类指针还原派生视图 */
static inline user_home_page_ui_t *home_ui(view_t *self)
{
    return container_of(self, user_home_page_ui_t, base);
}

/* 双按钮弹窗的按钮回调(在弹窗关闭前调用) */
static void popup_cancel_cb(void *user_data)
{
    (void)user_data;
}

static void popup_ok_cb(void *user_data)
{
    (void)user_data;
}

/* 创建主界面控件(幂等) */
static void home_init(view_t *self)
{
    user_home_page_ui_t *ui = home_ui(self);

    if (ui->screen != NULL)
    {
        return;
    }

    ui->screen = lv_obj_create(NULL);

    /* 根容器(全屏, 方便管理控件) */
    lv_obj_t *object = lv_obj_create(ui->screen);
    lv_obj_set_size(object, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 0, LV_PART_MAIN);
    /* 主题的card样式默认有16px内边距, 会把子控件整体右/下偏移, 这里清零,
     * 保证 set_pos 的坐标就是相对屏幕左上角 */
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);

    /* ==================== 相机启动按钮 ==================== */
    lv_obj_t *camera_btn = lv_btn_create(object);
    lv_obj_set_size(camera_btn, 60, 60);
    lv_obj_set_pos(camera_btn, 35, 210);

    lv_obj_t *camera_label = lv_label_create(camera_btn);
    lv_label_set_text(camera_label, "CAM");
    lv_obj_center(camera_label);

    view_bind_click(camera_btn, "cam", 0);

    /* ==================== 命令行页面按钮 ==================== */
    lv_obj_t *shell_btn = lv_btn_create(object);
    lv_obj_set_size(shell_btn, 60, 60);
    lv_obj_set_pos(shell_btn, 130, 210);

    lv_obj_t *shell_label = lv_label_create(shell_btn);
    lv_label_set_text(shell_label, "shell");
    lv_obj_center(shell_label);

    view_bind_click(shell_btn, "shell", 0);

    /* ==================== 视频页面按钮 ==================== */
    lv_obj_t *video_btn = lv_btn_create(object);
    lv_obj_set_size(video_btn, 60, 60);
    lv_obj_set_pos(video_btn, 225, 210);

    lv_obj_t *video_label = lv_label_create(video_btn);
    lv_label_set_text(video_label, "VIDEO");
    lv_obj_center(video_label);

    view_bind_click(video_btn, "video", 0);

    /* ==================== U盘模式按钮 ==================== */
    lv_obj_t *usb_btn = lv_btn_create(object);
    lv_obj_set_size(usb_btn, 60, 60);
    lv_obj_set_pos(usb_btn, 35, 290);

    lv_obj_t *usb_label = lv_label_create(usb_btn);
    lv_label_set_text(usb_label, "USB");
    lv_obj_center(usb_label);

    view_bind_click(usb_btn, "usb", 0);

    /* ==================== 录音页按钮 ==================== */
    lv_obj_t *recorder_btn = lv_btn_create(object);
    lv_obj_set_size(recorder_btn, 60, 60);
    lv_obj_set_pos(recorder_btn, 130, 290);

    lv_obj_t *recorder_label = lv_label_create(recorder_btn);
    lv_label_set_text(recorder_label, "REC");
    lv_obj_center(recorder_label);

    view_bind_click(recorder_btn, "rec", 0);

    /* ==================== WiFi连接页按钮 ==================== */
    lv_obj_t *wifi_btn = lv_btn_create(object);
    lv_obj_set_size(wifi_btn, 60, 60);
    lv_obj_set_pos(wifi_btn, 225, 290);

    lv_obj_t *wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, "WIFI");
    lv_obj_center(wifi_label);

    view_bind_click(wifi_btn, "wifi", 0);

    /* ==================== 三种弹窗测试按钮(第一行, 放在页面最上面) ==================== */
    lv_obj_t *msg_btn = lv_btn_create(object);
    lv_obj_set_size(msg_btn, 60, 60);
    lv_obj_set_pos(msg_btn, 35, 50);

    lv_obj_t *msg_label = lv_label_create(msg_btn);
    lv_label_set_text(msg_label, "MSG");
    lv_obj_center(msg_label);

    view_bind_click(msg_btn, "notice", 0);

    lv_obj_t *dialog_btn = lv_btn_create(object);
    lv_obj_set_size(dialog_btn, 60, 60);
    lv_obj_set_pos(dialog_btn, 130, 50);

    lv_obj_t *dialog_label = lv_label_create(dialog_btn);
    lv_label_set_text(dialog_label, "DIALOG");
    lv_obj_center(dialog_label);

    view_bind_click(dialog_btn, "dialog", 0);

    lv_obj_t *toast_btn = lv_btn_create(object);
    lv_obj_set_size(toast_btn, 60, 60);
    lv_obj_set_pos(toast_btn, 225, 50);

    lv_obj_t *toast_label = lv_label_create(toast_btn);
    lv_label_set_text(toast_label, "TOAST");
    lv_obj_center(toast_label);

    view_bind_click(toast_btn, "toast", 0);

    /* ==================== 系统级弹窗测试按钮(第二行居中) ==================== */
    lv_obj_t *msg2_btn = lv_btn_create(object);
    lv_obj_set_size(msg2_btn, 60, 60);
    lv_obj_set_pos(msg2_btn, 130, 130);

    lv_obj_t *msg2_label = lv_label_create(msg2_btn);
    lv_label_set_text(msg2_label, "MSG2");
    lv_obj_center(msg2_label);

    view_bind_click(msg2_btn, "notice2", 0);
}

/* 销毁主界面控件 */
static void home_deinit(view_t *self)
{
    user_home_page_ui_t *ui = home_ui(self);

    if (ui->screen == NULL)
    {
        return;
    }

    lv_obj_del(ui->screen);
    ui->screen = NULL;
}

/**********************
 *   生命周期回调
 **********************/

/* 首次进入: 创建控件并载入屏幕 */
static void home_on_start(view_t *self)
{
    user_home_page_ui_t *ui = home_ui(self);

    home_init(self);

    lv_scr_load(ui->screen);
    lv_obj_invalidate(ui->screen);
}

/* 从挂起恢复: 资源还在, 只需重新载入屏幕 */
static void home_on_resume(view_t *self)
{
    user_home_page_ui_t *ui = home_ui(self);

    if (ui->screen != NULL)
    {
        lv_scr_load(ui->screen);
        lv_obj_invalidate(ui->screen);
    }
}

/* 停止页面 */
static void home_on_stop(view_t *self)
{
    home_deinit(self);
}

/* 点击分发: 所有按钮点击在此按 click_id 字符串统一处理 */
static void home_on_click(view_t *self, const char *click_id, intptr_t arg)
{
    (void)self;
    (void)arg;

    if (click_id == NULL)
    {
        return;
    }

    if (strcmp(click_id, "cam") == 0)
    {
        if (s_camera_cb != NULL)
        {
            s_camera_cb();          /* 进入摄像头页 */
        }
    }
    else if (strcmp(click_id, "shell") == 0)
    {
        manager_gopage(SHELL_PAGE_ID);
    }
    else if (strcmp(click_id, "video") == 0)
    {
        manager_gopage(VIDEO_PAGE_ID);
    }
    else if (strcmp(click_id, "usb") == 0)
    {
        manager_gopage(USB_DISK_PAGE_ID);
    }
    else if (strcmp(click_id, "rec") == 0)
    {
        manager_gopage(RECORDER_PAGE_ID);
    }
    else if (strcmp(click_id, "wifi") == 0)
    {
        manager_gopage(WIFI_PAGE_ID);
    }
    else if (strcmp(click_id, "notice") == 0)
    {
        popup_notice_show("System0 notice!", 2000);     /* 顶部提示, 2秒后自动收回 */
    }
    else if (strcmp(click_id, "dialog") == 0)
    {
        popup_confirm_show("Are you sure you want to continue?",
                           "Cancel", "OK", popup_cancel_cb, popup_ok_cb, NULL);
    }
    else if (strcmp(click_id, "toast") == 0)
    {
        popup_toast_show("System1 toast", 2000);        /* 中心toast, 2秒后自动收回 */
    }
    else if (strcmp(click_id, "notice2") == 0)
    {
        popup_notice_show("System0 notice", 2000);      /* 验证应用级弹窗盖不住系统级弹窗 */
    }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void user_home_page_init(void)
{
    page_manager_init();
    page_register(&s_ui.base);
}

void user_home_page_set_camera_cb(user_home_page_camera_cb_t cb)
{
    s_camera_cb = cb;
}
