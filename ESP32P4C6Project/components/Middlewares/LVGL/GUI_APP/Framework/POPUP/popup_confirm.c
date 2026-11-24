/**
 * @file popup_confirm.c
 * @brief 底部双按钮确认弹窗实现: 应用级, 控件挂在 lv_layer_top()
 *
 * 每次 popup_confirm_show() 都新建一个独立实例(lv_mem_alloc), 多个实例
 * 在 popup_manager 的 ready 链表里按优先级排队/抢占, 出队后由 dispose 释放。
 * 本文件所有函数都必须在 LVGL 任务中调用。
 */

#include "popup_confirm.h"
#include "popup_base.h"
#include "popup_manager.h"
#include "view.h"
#include "container_of.h"
#include "lvgl.h"

#include <string.h>

#define POPUP_ID_CONFIRM           101                 /* 仅供日志调试 */
#define POPUP_ANIM_MS              250                 /* 滑入/滑出时长 */
#define POPUP_CONFIRM_DESC_MAX     128
#define POPUP_BTN_TEXT_MAX         24

#define POPUP_CONFIRM_W            300                 /* 确认弹窗尺寸/位置 */
#define POPUP_CONFIRM_HEIGHT            200
#define POPUP_CONFIRM_MARGIN_X     ((LV_HOR_RES - POPUP_CONFIRM_W) / 2)
#define POPUP_CONFIRM_REST_Y       (LV_VER_RES - POPUP_CONFIRM_HEIGHT - 20)
#define POPUP_BTN_W                120                 /* 按钮尺寸/左右下边距 */
#define POPUP_BTN_H                44
#define POPUP_BTN_MARGIN           20

/* 底部确认弹窗(每次 show 创建一个实例) */
typedef struct popup_confirm_ui
{
    popup_base_t base;                                  /* 弹窗基类(必须放第一位) */
    struct popup_confirm_ui *next;                      /* 实例链表(hide/visible 遍历) */
    lv_obj_t *mask;                                     /* 全屏遮罩(模态, 挡住底层点击) */
    lv_obj_t *panel;                                    /* 底部面板 */
    lv_obj_t *desc;                                     /* 居中描述文本 */
    lv_obj_t *left_btn;                                 /* 左按钮 */
    lv_obj_t *left_label;
    lv_obj_t *right_btn;                                /* 右按钮 */
    lv_obj_t *right_label;
    bool visible;
    bool hiding;
    char desc_text[POPUP_CONFIRM_DESC_MAX];
    char left_text[POPUP_BTN_TEXT_MAX];
    char right_text[POPUP_BTN_TEXT_MAX];
    popup_action_cb_t left_cb;
    popup_action_cb_t right_cb;
    void *user_data;
} popup_confirm_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void confirm_on_start(view_t *self);
static void confirm_on_stop(view_t *self);
static void confirm_slide_in(popup_confirm_ui_t *ui);
static bool confirm_hide_instance(popup_confirm_ui_t *ui);
static void confirm_dispose(view_t *self);
static void confirm_out_ready_cb(lv_anim_t *a);
static void confirm_btn_event_cb(lv_event_t *e);

/**********************
 *  STATIC VARIABLES
 **********************/
/* 实例模板: show 时拷贝一份再填数据 */
static const popup_confirm_ui_t s_confirm_template =
{
    .base =
    {
        .view =
        {
            .id       = POPUP_ID_CONFIRM,
            .name     = "popup_confirm",
            .on_start = confirm_on_start,
            .on_stop  = confirm_on_stop,
        },
        .level    = POPUP_LEVEL_APP,
        .priority = APP_PRIORITY0,
        .value    = 0,                              /* 常驻, 由用户点击按钮关闭 */
        .dispose  = confirm_dispose,
    },
};

static popup_confirm_ui_t *s_instances;                 /* 存活实例链表头 */

/**********************
 *   内部辅助
 **********************/

/* 由基类指针还原派生弹窗 */
static inline popup_confirm_ui_t *confirm_ui(view_t *self)
{
    return container_of(popup_base_of(self), popup_confirm_ui_t, base);
}

/* 纵向滑动动画(Y方向), ready/user_data可为NULL */
static void popup_slide_y(lv_obj_t *obj, int32_t from, int32_t to,
                          lv_anim_ready_cb_t ready, void *user_data)
{
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, POPUP_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, user_data);

    if (ready != NULL)
    {
        lv_anim_set_ready_cb(&a, ready);
    }

    lv_anim_start(&a);
}

static void confirm_out_ready_cb(lv_anim_t *a)
{
    popup_confirm_ui_t *ui = lv_anim_get_user_data(a);

    if (ui != NULL)
    {
        g_popup_manager.destroy(&ui->base.view);
    }
}

/* 从屏幕下方滑入到停靠位置 */
static void confirm_slide_in(popup_confirm_ui_t *ui)
{
    lv_anim_del(ui->panel, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_obj_set_y(ui->panel, LV_VER_RES);
    popup_slide_y(ui->panel, LV_VER_RES, POPUP_CONFIRM_REST_Y, NULL, NULL);
    ui->hiding = false;
}

/* 收回一个实例: 运行中则滑出(返回true), 排队中则直接出队释放(返回false) */
static bool confirm_hide_instance(popup_confirm_ui_t *ui)
{
    if (!ui->visible)
    {
        if (popup_manager_is_open(&ui->base.view))
        {
            g_popup_manager.destroy(&ui->base.view);   /* 出队并释放 */
        }

        return false;
    }

    if (ui->hiding)
    {
        return false;
    }

    ui->hiding = true;

    lv_anim_del(ui->panel, (lv_anim_exec_xcb_t)lv_obj_set_y);
    popup_slide_y(ui->panel, lv_obj_get_y(ui->panel),
                  LV_VER_RES, confirm_out_ready_cb, ui);

    return true;
}

/* 按钮点击: 先开始退场, 再跑回调; 回调之后不再访问 ui
 * (回调里可能切页, 切页会同步关闭并释放本实例) */
static void confirm_btn_event_cb(lv_event_t *e)
{
    popup_confirm_ui_t *ui = (popup_confirm_ui_t *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    popup_action_cb_t cb;
    void *user_data;

    if (ui->hiding)
    {
        return;
    }

    cb        = (btn == ui->right_btn) ? ui->right_cb : ui->left_cb;
    user_data = ui->user_data;

    confirm_hide_instance(ui);

    if (cb != NULL)
    {
        cb(user_data);              /* 之后不要再碰 ui */
    }
}

/* 创建确认弹窗控件(开始运行时调用) */
static void confirm_on_start(view_t *self)
{
    popup_confirm_ui_t *ui = confirm_ui(self);

    /* 应用级弹窗画在 top 层, 会被系统级弹窗盖住 */
    /* 全屏遮罩: 半透明黑, 可点击以挡住底层页面(模态) */
    ui->mask = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ui->mask, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(ui->mask, 0, 0);
    lv_obj_set_style_radius(ui->mask, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->mask, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->mask, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->mask, LV_OPA_40, LV_PART_MAIN);
    lv_obj_clear_flag(ui->mask, LV_OBJ_FLAG_SCROLLABLE);

    /* 底部面板 */
    ui->panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ui->panel, POPUP_CONFIRM_W, POPUP_CONFIRM_HEIGHT);
    lv_obj_set_style_radius(ui->panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->panel, lv_color_make(0x2B, 0x2B, 0x2B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui->panel, 0, LV_PART_MAIN);   /* 对齐按整块面板算, 避免按钮挤在一起 */
    lv_obj_clear_flag(ui->panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui->panel, POPUP_CONFIRM_MARGIN_X, LV_VER_RES);

    /* 描述文本(居中, 自动换行) */
    ui->desc = lv_label_create(ui->panel);
    lv_obj_set_width(ui->desc, POPUP_CONFIRM_W - 2 * POPUP_BTN_MARGIN);
    lv_label_set_long_mode(ui->desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(ui->desc, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(ui->desc, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->desc, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_label_set_text(ui->desc, ui->desc_text);
    lv_obj_align(ui->desc, LV_ALIGN_TOP_MID, 0, 24);

    /* 左按钮 */
    ui->left_btn = lv_btn_create(ui->panel);
    lv_obj_set_size(ui->left_btn, POPUP_BTN_W, POPUP_BTN_H);
    lv_obj_align(ui->left_btn, LV_ALIGN_BOTTOM_LEFT, POPUP_BTN_MARGIN, -POPUP_BTN_MARGIN);
    lv_obj_add_event_cb(ui->left_btn, confirm_btn_event_cb, LV_EVENT_CLICKED, ui);

    ui->left_label = lv_label_create(ui->left_btn);
    lv_label_set_text(ui->left_label, ui->left_text);
    lv_obj_center(ui->left_label);

    /* 右按钮 */
    ui->right_btn = lv_btn_create(ui->panel);
    lv_obj_set_size(ui->right_btn, POPUP_BTN_W, POPUP_BTN_H);
    lv_obj_align(ui->right_btn, LV_ALIGN_BOTTOM_RIGHT, -POPUP_BTN_MARGIN, -POPUP_BTN_MARGIN);
    lv_obj_add_event_cb(ui->right_btn, confirm_btn_event_cb, LV_EVENT_CLICKED, ui);

    ui->right_label = lv_label_create(ui->right_btn);
    lv_label_set_text(ui->right_label, ui->right_text);
    lv_obj_center(ui->right_label);

    ui->visible = true;
    ui->hiding  = false;

    confirm_slide_in(ui);
}

/* 删除确认弹窗控件(实例内存由 dispose 释放) */
static void confirm_on_stop(view_t *self)
{
    popup_confirm_ui_t *ui = confirm_ui(self);

    if (ui->panel != NULL)
    {
        lv_obj_del(ui->panel);
        ui->panel = NULL;
    }

    if (ui->mask != NULL)
    {
        lv_obj_del(ui->mask);
        ui->mask = NULL;
    }

    ui->desc        = NULL;
    ui->left_btn    = NULL;
    ui->left_label  = NULL;
    ui->right_btn   = NULL;
    ui->right_label = NULL;
    ui->visible     = false;
    ui->hiding      = false;
}

/* 出队后由管理器清理: 只从本类型实例链表摘除, 内存由 popup_dispose 统一释放 */
static void confirm_dispose(view_t *self)
{
    popup_confirm_ui_t *ui = confirm_ui(self);
    popup_confirm_ui_t **pp = &s_instances;

    while (*pp != NULL)
    {
        if (*pp == ui)
        {
            *pp = ui->next;
            break;
        }

        pp = &(*pp)->next;
    }
}

void popup_confirm_show(const char *desc,
                        const char *left_text,
                        const char *right_text,
                        popup_action_cb_t left_cb,
                        popup_action_cb_t right_cb,
                        void *user_data)
{
    popup_confirm_ui_t prototype = s_confirm_template;   /* 先填好数据再上架 */
    popup_confirm_ui_t *ui;

    if (desc == NULL)
    {
        return;
    }

    strlcpy(prototype.desc_text, desc, sizeof(prototype.desc_text));
    strlcpy(prototype.left_text, (left_text != NULL) ? left_text : "",
            sizeof(prototype.left_text));
    strlcpy(prototype.right_text, (right_text != NULL) ? right_text : "",
            sizeof(prototype.right_text));
    prototype.left_cb   = left_cb;
    prototype.right_cb  = right_cb;
    prototype.user_data = user_data;

    ui = (popup_confirm_ui_t *)app_popup_create(
             &prototype.base, sizeof(prototype), APP_PRIORITY0);

    if (ui == NULL)
    {
        return;
    }

    ui->next    = s_instances;                       /* 登记到本类型实例链表 */
    s_instances = ui;
}

void popup_confirm_hide(void)
{
    popup_confirm_ui_t *ui = s_instances;

    while (ui != NULL)
    {
        popup_confirm_ui_t *next = ui->next;    /* 排队实例可能被立即释放 */

        confirm_hide_instance(ui);
        ui = next;
    }
}

bool popup_confirm_visible(void)
{
    popup_confirm_ui_t *ui;

    for (ui = s_instances; ui != NULL; ui = ui->next)
    {
        if (ui->visible)
        {
            return true;
        }
    }

    return false;
}
