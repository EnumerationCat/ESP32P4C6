/**
 * @file popup_toast.c
 * @brief 屏幕中心提示弹窗实现: 系统级, 控件挂在 lv_layer_sys()
 *
 * 生命周期由 popup_manager 管理:
 *   open  -> on_start 创建控件并淡入, 按 POPUP_LEVEL_SYS 入队
 *   close -> on_stop 删除控件, 不影响页面栈
 * 本文件所有函数都必须在 LVGL 任务中调用。
 */

#include "popup_toast.h"
#include "popup_base.h"
#include "popup_manager.h"
#include "view.h"
#include "container_of.h"
#include "lvgl.h"

#include <string.h>

#define POPUP_ID_TOAST             102                 /* 仅供日志调试 */
#define POPUP_ANIM_MS              250                 /* 淡入/淡出时长 */
#define POPUP_TOAST_TEXT_MAX       96

#define POPUP_TOAST_W              240                 /* 提示弹窗尺寸(高度随文字自适应) */
#define POPUP_TOAST_PAD            16                  /* 显示文字时面板的水平内边距 */

/* 屏幕中心提示弹窗 */
typedef struct
{
    popup_base_t base;                                  /* 弹窗基类(必须放第一位) */
    lv_obj_t *panel;                                    /* 提示面板(在屏幕中心) */
    lv_obj_t *label;                                    /* 居中文字 */
    lv_timer_t *timer;                                  /* 自动收回定时器 */
    bool visible;                                       /* 正在显示(含淡出过程) */
    bool hiding;                                        /* 正在淡出 */
    char text[POPUP_TOAST_TEXT_MAX];                    /* 待显示文字(on_start前先缓存) */
    uint32_t duration_ms;                               /* 自动收回时间, 0=常驻 */
} popup_toast_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void toast_on_start(view_t *self);
static void toast_on_stop(view_t *self);
static void toast_fade_in(popup_toast_ui_t *ui);
static void toast_start_timer(popup_toast_ui_t *ui);
static void toast_out_ready_cb(lv_anim_t *a);

/**********************
 *  STATIC VARIABLES
 **********************/
static popup_toast_ui_t s_toast =
{
    .base =
    {
        .view =
        {
            .id       = POPUP_ID_TOAST,
            .name     = "popup_toast",
            .on_start = toast_on_start,
            .on_stop  = toast_on_stop,
        },
        .level    = POPUP_LEVEL_SYS,
        .priority = SYSTEM_PRIORITY1,
    },
};

/**********************
 *   内部辅助
 **********************/

/* 由基类指针还原派生弹窗 */
static inline popup_toast_ui_t *toast_ui(view_t *self)
{
    return container_of(popup_base_of(self), popup_toast_ui_t, base);
}

/* 透明度动画执行回调(面板整体淡入淡出, 文字跟随) */
static void popup_opa_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, LV_PART_MAIN);
}

/* 透明度动画, ready可为NULL */
static void popup_fade(lv_obj_t *obj, lv_opa_t from, lv_opa_t to,
                       lv_anim_ready_cb_t ready)
{
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, popup_opa_anim_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, POPUP_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

    if (ready != NULL)
    {
        lv_anim_set_ready_cb(&a, ready);
    }

    lv_anim_start(&a);
}

static void toast_out_ready_cb(lv_anim_t *a)
{
    (void)a;
    popup_manager_close(&s_toast.base.view);
}

/* 自动收回(定时器回调) */
static void toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    popup_toast_hide();
}

/* 以完全透明起步, 淡入到不透明 */
static void toast_fade_in(popup_toast_ui_t *ui)
{
    lv_anim_del(ui->panel, popup_opa_anim_cb);
    lv_obj_set_style_opa(ui->panel, LV_OPA_TRANSP, LV_PART_MAIN);
    popup_fade(ui->panel, LV_OPA_TRANSP, LV_OPA_COVER, NULL);
    ui->hiding = false;
}

/* 重置自动收回定时器 */
static void toast_start_timer(popup_toast_ui_t *ui)
{
    if (ui->timer != NULL)
    {
        lv_timer_del(ui->timer);
        ui->timer = NULL;
    }

    if (ui->duration_ms > 0)
    {
        ui->timer = lv_timer_create(toast_timer_cb, ui->duration_ms, NULL);
    }
}

/* 创建提示弹窗控件(启动时调用) */
static void toast_on_start(view_t *self)
{
    popup_toast_ui_t *ui = toast_ui(self);

    /* 系统级弹窗画在 sys 层, 永远在应用级弹窗和页面之上 */
    ui->panel = lv_obj_create(lv_layer_sys());
    lv_obj_set_size(ui->panel, POPUP_TOAST_W, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(ui->panel, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->panel, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ui->panel, POPUP_TOAST_PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ui->panel, 12, LV_PART_MAIN);
    lv_obj_clear_flag(ui->panel, LV_OBJ_FLAG_SCROLLABLE);

    ui->label = lv_label_create(ui->panel);
    lv_obj_set_width(ui->label, POPUP_TOAST_W - 2 * POPUP_TOAST_PAD);
    lv_label_set_long_mode(ui->label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(ui->label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(ui->label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_label_set_text(ui->label, ui->text);
    lv_obj_center(ui->label);

    /* 整个面板在屏幕正中央 */
    lv_obj_center(ui->panel);

    ui->visible = true;
    ui->hiding  = false;

    toast_fade_in(ui);
    toast_start_timer(ui);
}

/* 删除提示弹窗控件 */
static void toast_on_stop(view_t *self)
{
    popup_toast_ui_t *ui = toast_ui(self);

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

void popup_toast_show(const char *text, uint32_t duration_ms)
{
    if (text == NULL)
    {
        return;
    }

    strlcpy(s_toast.text, text, sizeof(s_toast.text));
    s_toast.duration_ms = duration_ms;

    if (s_toast.visible)
    {
        if (s_toast.label != NULL)
        {
            lv_label_set_text(s_toast.label, s_toast.text);
        }

        if (s_toast.hiding)
        {
            toast_fade_in(&s_toast);        /* 收回途中又显示: 重新淡入 */
        }

        toast_start_timer(&s_toast);
        return;
    }

    popup_systempopup_create(&s_toast.base.view, SYSTEM_PRIORITY1);
}

void popup_toast_hide(void)
{
    if (!s_toast.visible || s_toast.hiding)
    {
        return;
    }

    s_toast.hiding = true;

    if (s_toast.timer != NULL)
    {
        lv_timer_del(s_toast.timer);
        s_toast.timer = NULL;
    }

    lv_anim_del(s_toast.panel, popup_opa_anim_cb);
    popup_fade(s_toast.panel,
               lv_obj_get_style_opa(s_toast.panel, LV_PART_MAIN),
               LV_OPA_TRANSP, toast_out_ready_cb);
}

bool popup_toast_visible(void)
{
    return s_toast.visible;
}
