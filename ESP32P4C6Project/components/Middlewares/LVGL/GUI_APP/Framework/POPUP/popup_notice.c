/**
 * @file popup_notice.c
 * @brief 顶部滑入提示弹窗实现: 系统级, 控件挂在 lv_layer_sys()
 *
 * 生命周期由 popup_manager 管理:
 *   open  -> on_start 创建控件并滑入, 按 POPUP_LEVEL_SYS 入队
 *   close -> on_stop 删除控件, 不影响页面栈
 * 本文件所有函数都必须在 LVGL 任务中调用。
 */

#include "popup_notice.h"
#include "popup_base.h"
#include "popup_manager.h"
#include "view.h"
#include "container_of.h"
#include "lvgl.h"

#include <string.h>

#define POPUP_ID_NOTICE            100                 /* 仅供日志调试 */
#define POPUP_ANIM_MS              250                 /* 滑入/滑出时长 */
#define POPUP_NOTICE_TEXT_MAX      64

#define POPUP_NOTICE_W             280                 /* 提示弹窗尺寸/位置 */
#define POPUP_NOTICE_HEIGHT             96
#define POPUP_NOTICE_MARGIN_X      ((LV_HOR_RES - POPUP_NOTICE_W) / 2)
#define POPUP_NOTICE_REST_Y        56

/* 顶部提示弹窗 */
typedef struct
{
    popup_base_t base;                                  /* 弹窗基类(必须放第一位) */
    lv_obj_t *panel;                                    /* 提示面板 */
    lv_obj_t *label;                                    /* 居中文字 */
    lv_timer_t *timer;                                  /* 自动收回定时器 */
    bool visible;                                       /* 正在显示(含滑出过程) */
    bool hiding;                                        /* 正在滑出 */
    char text[POPUP_NOTICE_TEXT_MAX];                   /* 待显示文字(on_start前先缓存) */
    uint32_t duration_ms;                               /* 自动收回时间, 0=常驻 */
} popup_notice_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void notice_on_start(view_t *self);
static void notice_on_stop(view_t *self);
static void notice_slide_in(popup_notice_ui_t *ui);
static void notice_start_timer(popup_notice_ui_t *ui);
static void notice_out_ready_cb(lv_anim_t *a);

/**********************
 *  STATIC VARIABLES
 **********************/
static popup_notice_ui_t s_notice =
{
    .base =
    {
        .view =
        {
            .id       = POPUP_ID_NOTICE,
            .name     = "popup_notice",
            .on_start = notice_on_start,
            .on_stop  = notice_on_stop,
        },
        .level    = POPUP_LEVEL_SYS,
        .priority = SYSTEM_PRIORITY0,
    },
};

/**********************
 *   内部辅助
 **********************/

/* 由基类指针还原派生弹窗 */
static inline popup_notice_ui_t *notice_ui(view_t *self)
{
    return container_of(popup_base_of(self), popup_notice_ui_t, base);
}

/* 纵向滑动动画(Y方向), ready可为NULL */
static void popup_slide_y(lv_obj_t *obj, int32_t from, int32_t to,
                          lv_anim_ready_cb_t ready)
{
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, POPUP_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

    if (ready != NULL)
    {
        lv_anim_set_ready_cb(&a, ready);
    }

    lv_anim_start(&a);
}

static void notice_out_ready_cb(lv_anim_t *a)
{
    (void)a;
    popup_manager_close(&s_notice.base.view);
}

/* 自动收回(定时器回调) */
static void notice_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    popup_notice_hide();
}

/* 从屏幕上方滑入到停靠位置 */
static void notice_slide_in(popup_notice_ui_t *ui)
{
    lv_anim_del(ui->panel, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_obj_set_y(ui->panel, -POPUP_NOTICE_HEIGHT);
    popup_slide_y(ui->panel, -POPUP_NOTICE_HEIGHT, POPUP_NOTICE_REST_Y, NULL);
    ui->hiding = false;
}

/* 重置自动收回定时器 */
static void notice_start_timer(popup_notice_ui_t *ui)
{
    if (ui->timer != NULL)
    {
        lv_timer_del(ui->timer);
        ui->timer = NULL;
    }

    if (ui->duration_ms > 0)
    {
        ui->timer = lv_timer_create(notice_timer_cb, ui->duration_ms, NULL);
    }
}

/* 创建提示弹窗控件(启动时调用) */
static void notice_on_start(view_t *self)
{
    popup_notice_ui_t *ui = notice_ui(self);

    /* 系统级弹窗画在 sys 层, 永远在应用级弹窗和页面之上 */
    ui->panel = lv_obj_create(lv_layer_sys());
    lv_obj_set_size(ui->panel, POPUP_NOTICE_W, POPUP_NOTICE_HEIGHT);
    lv_obj_set_style_radius(ui->panel, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->panel, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ui->panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui->panel, POPUP_NOTICE_MARGIN_X, -POPUP_NOTICE_HEIGHT);

    ui->label = lv_label_create(ui->panel);
    lv_obj_set_width(ui->label, POPUP_NOTICE_W - 32);
    lv_label_set_long_mode(ui->label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(ui->label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(ui->label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_label_set_text(ui->label, ui->text);
    lv_obj_center(ui->label);

    ui->visible = true;
    ui->hiding  = false;

    notice_slide_in(ui);
    notice_start_timer(ui);
}

/* 删除提示弹窗控件 */
static void notice_on_stop(view_t *self)
{
    popup_notice_ui_t *ui = notice_ui(self);

    if (ui->timer != NULL)
    {
        lv_timer_del(ui->timer);
        ui->timer = NULL;
    }

    if (ui->panel != NULL)
    {
        lv_obj_del(ui->panel);          /* lv_obj_del 会同时删除该对象上的动画 */
        ui->panel = NULL;
    }

    ui->label   = NULL;
    ui->visible = false;
    ui->hiding  = false;
}

void popup_notice_show(const char *text, uint32_t duration_ms)
{
    if (text == NULL)
    {
        return;
    }

    strlcpy(s_notice.text, text, sizeof(s_notice.text));
    s_notice.duration_ms = duration_ms;

    if (s_notice.visible)
    {
        if (s_notice.label != NULL)
        {
            lv_label_set_text(s_notice.label, s_notice.text);
        }

        if (s_notice.hiding)
        {
            notice_slide_in(&s_notice);         /* 收回途中又显示: 重新滑入 */
        }

        notice_start_timer(&s_notice);
        return;
    }

    popup_systempopup_create(&s_notice.base.view, SYSTEM_PRIORITY0);
}

void popup_notice_hide(void)
{
    if (!s_notice.visible || s_notice.hiding)
    {
        return;
    }

    s_notice.hiding = true;

    if (s_notice.timer != NULL)
    {
        lv_timer_del(s_notice.timer);
        s_notice.timer = NULL;
    }

    lv_anim_del(s_notice.panel, (lv_anim_exec_xcb_t)lv_obj_set_y);
    popup_slide_y(s_notice.panel, lv_obj_get_y(s_notice.panel),
                  -POPUP_NOTICE_HEIGHT, notice_out_ready_cb);
}

bool popup_notice_visible(void)
{
    return s_notice.visible;
}
