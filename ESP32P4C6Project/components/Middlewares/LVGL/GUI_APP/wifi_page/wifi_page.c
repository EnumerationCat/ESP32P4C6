/**
 * @file wifi_page.c
 * @brief WiFi连接页: 下拉框选择热点 + 密码输入框/键盘 + 连接
 *
 * 与其它页面相同的 C 式继承: base 必须是第一个成员。
 * 扫描由 wifi_manager_scan() 在独立任务中完成, 结果写入 wifi_scan_model
 * (Model层, 带锁缓存); 本页只负责取走字符串渲染下拉框并产生事件。
 */

#include "wifi_page.h"
#include "user_home_page.h"
#include "page_manager.h"
#include "view.h"
#include "container_of.h"
#include "lvgl.h"
#include "wifi_manager.h"
#include "ap_wifi.h"
#include "app_event.h"
#include "wifi_scan_model.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "wifi_page";

#define WIFI_SCAN_WAIT_US       (12 * 1000 * 1000)          /* 扫描超时 */
#define WIFI_CONNECT_WAIT_US    (15 * 1000 * 1000)          /* 连接超时 */

/* 派生视图: base必须是第一个成员(C式继承) + LCD界面对象 */
typedef struct
{
    view_t base;                        /* Framework基类(必须放第一位) */
    lv_obj_t *screen;                   /* 连接页屏幕 */
    lv_obj_t *ap_dd;                    /* 热点下拉框 */
    lv_obj_t *pw_ta;                    /* 密码输入框 */
    lv_obj_t *kb;                       /* 软键盘 */
    lv_obj_t *status_label;             /* 状态提示 */

    char target_ssid[WIFI_SSID_LEN];    /* 正在尝试连接的SSID */
    bool connecting;                    /* 连接中(含超时判断) */
    bool last_connected;                /* 上一次查询到的连接状态 */
    int64_t connect_deadline;           /* 连接超时时刻 */
    int64_t scan_deadline;              /* 扫描超时时刻 */
    int64_t last_update_us;             /* 上次刷新时间(节流) */
} wifi_page_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void wifi_page_create(view_t *self);
static void wifi_page_destroy(view_t *self);
static void wifi_page_on_start(view_t *self);
static void wifi_page_on_resume(view_t *self);
static void wifi_page_on_stop(view_t *self);
static void wifi_page_on_update(view_t *self);
static void wifi_page_on_event(view_t *self, const char *event_id);

static wifi_page_ui_t s_ui =
{
    .base =
    {
        .id        = WIFI_PAGE_ID,
        .name      = "wifi",
        .init      = wifi_page_create,
        .deinit    = wifi_page_destroy,
        .on_start  = wifi_page_on_start,
        .on_resume = wifi_page_on_resume,
        .on_stop   = wifi_page_on_stop,
        .on_update = wifi_page_on_update,
        .on_event  = wifi_page_on_event,
    },
};

/**********************
 *   内部辅助
 **********************/

/* 由基类指针还原派生视图 */
static inline wifi_page_ui_t *wifi_ui(view_t *self)
{
    return container_of(self, wifi_page_ui_t, base);
}

/* 扫描完成回调(运行在扫描任务中, 不能操作LVGL): 写Model并投递事件 */
static void wifi_scan_cb(int num, wifi_ap_record_t *records)
{
    wifi_scan_model_on_result(num, records);

    /* 通知LVGL任务立即上屏; 页面不可见时由 on_update 兜底 */
    app_event_post("wifi_scan_done");
}

/* 发起扫描(幂等: 正在扫描时忽略) */
static void wifi_scan_start(wifi_page_ui_t *ui)
{
    if (!wifi_scan_model_begin())
    {
        return;
    }

    ui->scan_deadline = esp_timer_get_time() + WIFI_SCAN_WAIT_US;

    if (ui->status_label != NULL)
    {
        lv_label_set_text(ui->status_label, "Scanning...");
    }

    if (wifi_manager_scan(wifi_scan_cb) != ESP_OK)
    {
        wifi_scan_model_timeout();

        if (ui->status_label != NULL)
        {
            lv_label_set_text(ui->status_label, "Scan failed");
        }
    }
}

/* 把扫描结果刷到下拉框(LVGL任务中调用) */
static void wifi_apply_scan(wifi_page_ui_t *ui)
{
    int n = 0;
    const char *options = wifi_scan_model_take_options(&n);

    if (options == NULL)
    {
        return;
    }

    if (n <= 0 || options[0] == '\0')
    {
        lv_dropdown_set_options(ui->ap_dd, "No AP found");
        lv_dropdown_set_selected(ui->ap_dd, 0);
        lv_label_set_text(ui->status_label, "No AP found, tap SCAN");
        return;
    }

    lv_dropdown_set_options(ui->ap_dd, options);    /* 下拉框会复制该字符串 */
    lv_dropdown_set_selected(ui->ap_dd, 0);
    lv_label_set_text_fmt(ui->status_label, "%d network(s), select one", n);
}

/* 根据连接状态刷新提示 */
static void wifi_refresh_status(wifi_page_ui_t *ui, int64_t now)
{
    bool conn = wifi_manager_is_connect();

    if (conn)
    {
        ui->connecting     = false;
        ui->last_connected = true;

        if (ui->target_ssid[0] != '\0')
        {
            lv_label_set_text_fmt(ui->status_label, "Connected: %s", ui->target_ssid);
        }
        else
        {
            lv_label_set_text(ui->status_label, "Connected");
        }

        return;
    }

    if (ui->connecting)
    {
        if (now >= ui->connect_deadline)
        {
            ui->connecting = false;
            lv_label_set_text(ui->status_label, "Connect failed");
        }
        else
        {
            lv_label_set_text_fmt(ui->status_label, "Connecting: %s", ui->target_ssid);
        }

        return;
    }

    if (ui->last_connected)
    {
        ui->last_connected = false;
        lv_label_set_text(ui->status_label, "Disconnected");
    }
}

/* WiFi状态变化回调(运行在esp_event任务中, 只投递事件, 不碰LVGL) */
static void wifi_state_event_cb(WIFI_STATE state)
{
    app_event_post(state == WIFI_STATE_CONNECTED ? "wifi_connected"
                                                 : "wifi_disconnected");
}

/* 事件分发: 扫描完成/连接状态变化(由框架派发给当前页) */
static void wifi_page_on_event(view_t *self, const char *event_id)
{
    wifi_page_ui_t *ui = wifi_ui(self);

    if (event_id == NULL || ui->screen == NULL)
    {
        return;
    }

    if (strcmp(event_id, "wifi_scan_done") == 0)
    {
        if (wifi_scan_model_is_ready())
        {
            wifi_apply_scan(ui);        /* 扫描结果立即上屏, 不等 on_update 轮询 */
        }
    }
    else if (strcmp(event_id, "wifi_connected") == 0 ||
             strcmp(event_id, "wifi_disconnected") == 0)
    {
        wifi_refresh_status(ui, esp_timer_get_time());
    }
}

/**********************
 *   按钮回调
 **********************/

/* 左上角返回主界面 */
static void back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    manager_gopage(USER_HOME_PAGE_ID);
}

/* 右上角重新扫描 */
static void scan_btn_event_cb(lv_event_t *e)
{
    view_t *self = (view_t *)lv_event_get_user_data(e);

    wifi_scan_start(wifi_ui(self));
}

/* 连接所选热点 */
static void connect_btn_event_cb(lv_event_t *e)
{
    view_t *self = (view_t *)lv_event_get_user_data(e);
    wifi_page_ui_t *ui = wifi_ui(self);
    char ssid[WIFI_SSID_LEN];

    ssid[0] = '\0';
    lv_dropdown_get_selected_str(ui->ap_dd, ssid, sizeof(ssid));

    if (ssid[0] == '\0' || strcmp(ssid, "No AP found") == 0)
    {
        lv_label_set_text(ui->status_label, "Select a network first");
        return;
    }

    strlcpy(ui->target_ssid, ssid, sizeof(ui->target_ssid));
    ui->connecting       = true;
    ui->last_connected   = false;
    ui->connect_deadline = esp_timer_get_time() + WIFI_CONNECT_WAIT_US;

    lv_label_set_text_fmt(ui->status_label, "Connecting: %s", ssid);

    ESP_LOGI(TAG, "connect to %s", ssid);
    ap_wifi_set(ssid, lv_textarea_get_text(ui->pw_ta));
}

/* 断开当前热点 */
static void disconnect_btn_event_cb(lv_event_t *e)
{
    view_t *self = (view_t *)lv_event_get_user_data(e);
    wifi_page_ui_t *ui = wifi_ui(self);

    wifi_manager_disconnect();

    ui->connecting     = false;
    ui->last_connected = false;
    ui->target_ssid[0] = '\0';
    lv_label_set_text(ui->status_label, "Disconnected");
}

/**********************
 *   页面生命周期
 **********************/

/* 创建连接页控件(幂等) */
static void wifi_page_create(view_t *self)
{
    wifi_page_ui_t *ui = wifi_ui(self);
    lv_obj_t *title;
    lv_obj_t *back_btn;
    lv_obj_t *label;
    lv_obj_t *scan_btn;
    lv_obj_t *connect_btn;

    if (ui->screen != NULL)
    {
        return;
    }

    ui->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui->screen, lv_color_black(), LV_PART_MAIN);

    /* 软键盘先创建, 让后面的下拉列表绘制在它上层 */
    ui->kb = lv_keyboard_create(ui->screen);
    lv_obj_set_size(ui->kb, LV_HOR_RES, 200);
    lv_obj_align(ui->kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* 标题 */
    title = lv_label_create(ui->screen);
    lv_label_set_text(title, "WiFi");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 返回 */
    back_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(back_btn, 64, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(back_btn);
    lv_label_set_text(label, "Back");
    lv_obj_center(label);

    /* 重新扫描 */
    scan_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(scan_btn, 64, 36);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_add_event_cb(scan_btn, scan_btn_event_cb, LV_EVENT_CLICKED, self);

    label = lv_label_create(scan_btn);
    lv_label_set_text(label, "SCAN");
    lv_obj_center(label);

    /* 热点下拉框 */
    ui->ap_dd = lv_dropdown_create(ui->screen);
    lv_obj_set_size(ui->ap_dd, 280, 40);
    lv_obj_align(ui->ap_dd, LV_ALIGN_TOP_MID, 0, 56);
    lv_dropdown_set_options(ui->ap_dd, "Tap SCAN");

    /* 密码输入框(单行 + 密码模式) */
    ui->pw_ta = lv_textarea_create(ui->screen);
    lv_obj_set_size(ui->pw_ta, 280, 40);
    lv_obj_align(ui->pw_ta, LV_ALIGN_TOP_MID, 0, 104);
    lv_textarea_set_one_line(ui->pw_ta, true);
    lv_textarea_set_password_mode(ui->pw_ta, true);
    lv_textarea_set_max_length(ui->pw_ta, 63);
    lv_textarea_set_placeholder_text(ui->pw_ta, "Password");

    /* 连接 / 断开按钮(并排) */
    connect_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(connect_btn, 130, 44);
    lv_obj_align(connect_btn, LV_ALIGN_TOP_MID, -72, 152);
    lv_obj_add_event_cb(connect_btn, connect_btn_event_cb, LV_EVENT_CLICKED, self);

    label = lv_label_create(connect_btn);
    lv_label_set_text(label, "CONNECT");
    lv_obj_center(label);

    lv_obj_t *disconnect_btn = lv_btn_create(ui->screen);
    lv_obj_set_size(disconnect_btn, 130, 44);
    lv_obj_align(disconnect_btn, LV_ALIGN_TOP_MID, 72, 152);
    lv_obj_add_event_cb(disconnect_btn, disconnect_btn_event_cb, LV_EVENT_CLICKED, self);

    label = lv_label_create(disconnect_btn);
    lv_label_set_text(label, "DISCONNECT");
    lv_obj_center(label);

    /* 状态提示 */
    ui->status_label = lv_label_create(ui->screen);
    lv_label_set_text(ui->status_label, "Tap SCAN to list networks");
    lv_obj_set_style_text_color(ui->status_label, lv_color_make(0xB0, 0xB0, 0xB0), LV_PART_MAIN);
    lv_label_set_long_mode(ui->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui->status_label, 300);
    lv_obj_set_style_text_align(ui->status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(ui->status_label, LV_ALIGN_TOP_MID, 0, 206);

    /* 键盘的输入目标 */
    lv_keyboard_set_textarea(ui->kb, ui->pw_ta);
}

/* 销毁连接页控件 */
static void wifi_page_destroy(view_t *self)
{
    wifi_page_ui_t *ui = wifi_ui(self);

    if (ui->screen == NULL)
    {
        return;
    }

    lv_obj_del(ui->screen);
    ui->screen       = NULL;
    ui->ap_dd        = NULL;
    ui->pw_ta        = NULL;
    ui->kb           = NULL;
    ui->status_label = NULL;

    wifi_scan_model_clear_ready();
    ui->connecting   = false;
    ui->last_connected = false;
}

/* 首次进入 */
static void wifi_page_on_start(view_t *self)
{
    wifi_page_ui_t *ui = wifi_ui(self);

    wifi_page_create(self);
    wifi_refresh_status(ui, esp_timer_get_time());
    wifi_scan_start(ui);
    lv_scr_load(ui->screen);
    lv_obj_invalidate(ui->screen);
}

/* 从挂起恢复: 重新扫描以看到最新热点 */
static void wifi_page_on_resume(view_t *self)
{
    wifi_page_ui_t *ui = wifi_ui(self);

    if (ui->screen != NULL)
    {
        wifi_refresh_status(ui, esp_timer_get_time());
        wifi_scan_start(ui);
        lv_scr_load(ui->screen);
        lv_obj_invalidate(ui->screen);
    }
}

/* 停止页面 */
static void wifi_page_on_stop(view_t *self)
{
    wifi_page_destroy(self);
}

/* 周期刷新(仅当前可见页会被调用), 250ms 节流 */
static void wifi_page_on_update(view_t *self)
{
    wifi_page_ui_t *ui = wifi_ui(self);
    int64_t now = esp_timer_get_time();

    if (ui->screen == NULL || now - ui->last_update_us < 250 * 1000)
    {
        return;
    }

    ui->last_update_us = now;

    if (wifi_scan_model_is_ready())
    {
        wifi_apply_scan(ui);
    }
    else if (wifi_scan_model_is_busy() && now >= ui->scan_deadline)
    {
        wifi_scan_model_timeout();
        lv_label_set_text(ui->status_label, "Scan timeout, tap SCAN");
    }

    wifi_refresh_status(ui, now);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void wifi_page_init(void)
{
    static bool wifi_inited = false;

    if (!wifi_inited)
    {
        /* WiFi协议栈初始化(原在main): 启动STA但不连接任何热点,
         * 具体连哪个由本页面选择; 连接状态变更通过 app_event 投递给LVGL任务 */
        ap_wifi_init(wifi_state_event_cb);
        wifi_scan_model_init();
        wifi_inited = true;
    }

    page_manager_init();
    page_register(&s_ui.base);
}
