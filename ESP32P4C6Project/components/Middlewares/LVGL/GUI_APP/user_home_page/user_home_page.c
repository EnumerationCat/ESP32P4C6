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
#include "page_manager.h"
#include "view.h"
#include "container_of.h"
#include "lvgl.h"

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

/* 相机按钮事件回调 */
static void camera_btn_event_cb(lv_event_t *e)
{
    (void)e;

    if (s_camera_cb != NULL)
    {
        s_camera_cb();          /* 进入摄像头全屏预览, 触摸屏幕任意位置退出 */
    }
}

/* 命令行按钮事件回调 */
static void shell_btn_event_cb(lv_event_t *e)
{
    (void)e;

    /* 按ID切页: 在挂起栈中则恢复(on_resume), 否则重新启动(on_start) */
    manager_gopage(SHELL_PAGE_ID);
}

/* 视频页按钮事件回调 */
static void video_btn_event_cb(lv_event_t *e)
{
    (void)e;

    manager_gopage(VIDEO_PAGE_ID);
}

/* U盘模式页按钮事件回调 */
static void usb_disk_btn_event_cb(lv_event_t *e)
{
    (void)e;

    manager_gopage(USB_DISK_PAGE_ID);
}

/* 录音页按钮事件回调 */
static void recorder_btn_event_cb(lv_event_t *e)
{
    (void)e;

    manager_gopage(RECORDER_PAGE_ID);
}

/* WiFi连接页按钮事件回调 */
static void wifi_btn_event_cb(lv_event_t *e)
{
    (void)e;

    manager_gopage(WIFI_PAGE_ID);
}

/* 顶部提示弹窗测试: 2秒后自动收回 */
static void msg_btn_event_cb(lv_event_t *e)
{
    (void)e;

    popup_notice_show("Hello, this is a popup notice!", 2000);
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

/* 底部双按钮弹窗测试 */
static void dialog_btn_event_cb(lv_event_t *e)
{
    (void)e;

    popup_confirm_show("Are you sure you want to continue?",
                       "Cancel", "OK", popup_cancel_cb, popup_ok_cb, NULL);
}

/* 系统级提示弹窗测试: 用来验证应用级弹窗盖不住系统级弹窗 */
static void msg2_btn_event_cb(lv_event_t *e)
{
    (void)e;

    popup_notice_show("SYS level notice", 5000);
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
    lv_obj_set_pos(camera_btn, 35, 130);

    lv_obj_t *camera_label = lv_label_create(camera_btn);
    lv_label_set_text(camera_label, "CAM");
    lv_obj_center(camera_label);

    lv_obj_add_event_cb(camera_btn, camera_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== 命令行页面按钮 ==================== */
    lv_obj_t *shell_btn = lv_btn_create(object);
    lv_obj_set_size(shell_btn, 60, 60);
    lv_obj_set_pos(shell_btn, 130, 130);

    lv_obj_t *shell_label = lv_label_create(shell_btn);
    lv_label_set_text(shell_label, "shell");
    lv_obj_center(shell_label);

    lv_obj_add_event_cb(shell_btn, shell_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== 视频页面按钮 ==================== */
    lv_obj_t *video_btn = lv_btn_create(object);
    lv_obj_set_size(video_btn, 60, 60);
    lv_obj_set_pos(video_btn, 225, 130);

    lv_obj_t *video_label = lv_label_create(video_btn);
    lv_label_set_text(video_label, "VIDEO");
    lv_obj_center(video_label);

    lv_obj_add_event_cb(video_btn, video_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== U盘模式按钮 ==================== */
    lv_obj_t *usb_btn = lv_btn_create(object);
    lv_obj_set_size(usb_btn, 60, 60);
    lv_obj_set_pos(usb_btn, 35, 210);

    lv_obj_t *usb_label = lv_label_create(usb_btn);
    lv_label_set_text(usb_label, "USB");
    lv_obj_center(usb_label);

    lv_obj_add_event_cb(usb_btn, usb_disk_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== 录音页按钮 ==================== */
    lv_obj_t *recorder_btn = lv_btn_create(object);
    lv_obj_set_size(recorder_btn, 60, 60);
    lv_obj_set_pos(recorder_btn, 130, 210);

    lv_obj_t *recorder_label = lv_label_create(recorder_btn);
    lv_label_set_text(recorder_label, "REC");
    lv_obj_center(recorder_label);

    lv_obj_add_event_cb(recorder_btn, recorder_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== WiFi连接页按钮 ==================== */
    lv_obj_t *wifi_btn = lv_btn_create(object);
    lv_obj_set_size(wifi_btn, 60, 60);
    lv_obj_set_pos(wifi_btn, 225, 210);

    lv_obj_t *wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, "WIFI");
    lv_obj_center(wifi_label);

    lv_obj_add_event_cb(wifi_btn, wifi_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== 弹窗测试按钮(第三行居中两个) ==================== */
    lv_obj_t *msg_btn = lv_btn_create(object);
    lv_obj_set_size(msg_btn, 60, 60);
    lv_obj_set_pos(msg_btn, 67, 290);

    lv_obj_t *msg_label = lv_label_create(msg_btn);
    lv_label_set_text(msg_label, "MSG");
    lv_obj_center(msg_label);

    lv_obj_add_event_cb(msg_btn, msg_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *dialog_btn = lv_btn_create(object);
    lv_obj_set_size(dialog_btn, 60, 60);
    lv_obj_set_pos(dialog_btn, 193, 290);

    lv_obj_t *dialog_label = lv_label_create(dialog_btn);
    lv_label_set_text(dialog_label, "DIALOG");
    lv_obj_center(dialog_label);

    lv_obj_add_event_cb(dialog_btn, dialog_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== 系统级弹窗测试按钮(第四行居中) ==================== */
    lv_obj_t *msg2_btn = lv_btn_create(object);
    lv_obj_set_size(msg2_btn, 60, 60);
    lv_obj_set_pos(msg2_btn, 130, 370);

    lv_obj_t *msg2_label = lv_label_create(msg2_btn);
    lv_label_set_text(msg2_label, "MSG2");
    lv_obj_center(msg2_label);

    lv_obj_add_event_cb(msg2_btn, msg2_btn_event_cb, LV_EVENT_CLICKED, NULL);
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
