/**
 * @file popup_confirm.c
 * @brief 底部双按钮确认弹窗实现: 应用级, 控件挂在 lv_layer_top()
 *
 * 生命周期由 popup_manager 管理:
 *   open  -> on_start 创建控件并滑入, 按 POPUP_LEVEL_APP 入队
 *   close -> on_stop 删除控件, 不影响页面栈
 * 本文件所有函数都必须在 LVGL 任务中调用。
 */

#include "popup_confirm.h"
#include "popup_manager.h"
#include "view.h"
#include "container_of.h"
#include "lvgl.h"

#include <string.h>

#define POPUP_ID_CONFIRM           101                 /* 仅供日志调试 */
#define POPUP_ANIM_MS              250                 /* 滑入/滑出时长 */
#define POPUP_CONFIRM_DESC_MAX     128
#define POPUP_BTN_TEXT_MAX         24
#define POPUP_PRIORITY_CONFIRM     20                  /* 应用级队列内优先级 */

#define POPUP_CONFIRM_W            300                 /* 确认弹窗尺寸/位置 */
#define POPUP_CONFIRM_HEIGHT            200
#define POPUP_CONFIRM_MARGIN_X     ((LV_HOR_RES - POPUP_CONFIRM_W) / 2)
#define POPUP_CONFIRM_REST_Y       (LV_VER_RES - POPUP_CONFIRM_HEIGHT - 20)
#define POPUP_BTN_W                120                 /* 按钮尺寸/左右下边距 */
#define POPUP_BTN_H                44
#define POPUP_BTN_MARGIN           20

/* 底部确认弹窗 */
typedef struct
{
    view_t base;                                        /* Framework基类(必须放第一位) */
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
static void confirm_out_ready_cb(lv_anim_t *a);
static void confirm_btn_event_cb(lv_event_t *e);

/**********************
 *  STATIC VARIABLES
 **********************/
static popup_confirm_ui_t s_confirm =
{
    .base =
    {
        .id       = POPUP_ID_CONFIRM,
        .name     = "popup_confirm",
        .on_start = confirm_on_start,
        .on_stop  = confirm_on_stop,
    },
};

/**********************
 *   内部辅助
 **********************/

/* 由基类指针还原派生弹窗 */
static inline popup_confirm_ui_t *confirm_ui(view_t *self)
{
    return container_of(self, popup_confirm_ui_t, base);
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

static void confirm_out_ready_cb(lv_anim_t *a)
{
    (void)a;
    popup_manager_close(&s_confirm.base);
}

/* 从屏幕下方滑入到停靠位置 */
static void confirm_slide_in(popup_confirm_ui_t *ui)
{
    lv_anim_del(ui->panel, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_obj_set_y(ui->panel, LV_VER_RES);
    popup_slide_y(ui->panel, LV_VER_RES, POPUP_CONFIRM_REST_Y, NULL);
    ui->hiding = false;
}

/* 更新按钮文字(NULL按空串处理) */
static void confirm_set_btn_text(lv_obj_t *label, const char *text)
{
    if (label != NULL)
    {
        lv_label_set_text(label, (text != NULL) ? text : "");
    }
}

/* 按钮点击: 先跑回调(回调里可能切页, 切页会自动收掉弹窗), 再滑出关闭 */
static void confirm_btn_event_cb(lv_event_t *e)
{
    popup_confirm_ui_t *ui = (popup_confirm_ui_t *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    popup_action_cb_t cb = (btn == ui->right_btn) ? ui->right_cb : ui->left_cb;
    void *user_data = ui->user_data;

    if (cb != NULL)
    {
        cb(user_data);
    }

    popup_confirm_hide();
}

/* 创建确认弹窗控件(启动时调用) */
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

/* 删除确认弹窗控件 */
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

void popup_confirm_show(const char *desc,
                        const char *left_text,
                        const char *right_text,
                        popup_action_cb_t left_cb,
                        popup_action_cb_t right_cb,
                        void *user_data)
{
    if (desc == NULL)
    {
        return;
    }

    strlcpy(s_confirm.desc_text, desc, sizeof(s_confirm.desc_text));
    strlcpy(s_confirm.left_text, (left_text != NULL) ? left_text : "",
            sizeof(s_confirm.left_text));
    strlcpy(s_confirm.right_text, (right_text != NULL) ? right_text : "",
            sizeof(s_confirm.right_text));
    s_confirm.left_cb   = left_cb;
    s_confirm.right_cb  = right_cb;
    s_confirm.user_data = user_data;

    if (s_confirm.visible)
    {
        if (s_confirm.desc != NULL)
        {
            lv_label_set_text(s_confirm.desc, s_confirm.desc_text);
        }

        confirm_set_btn_text(s_confirm.left_label, s_confirm.left_text);
        confirm_set_btn_text(s_confirm.right_label, s_confirm.right_text);

        if (s_confirm.hiding)
        {
            confirm_slide_in(&s_confirm);       /* 收回途中又显示: 重新滑入 */
        }

        return;
    }

    popup_manager_open(&s_confirm.base, POPUP_LEVEL_APP, POPUP_PRIORITY_CONFIRM);
}

void popup_confirm_hide(void)
{
    if (!s_confirm.visible || s_confirm.hiding)
    {
        return;
    }

    s_confirm.hiding = true;

    lv_anim_del(s_confirm.panel, (lv_anim_exec_xcb_t)lv_obj_set_y);
    popup_slide_y(s_confirm.panel, lv_obj_get_y(s_confirm.panel),
                  LV_VER_RES, confirm_out_ready_cb);
}

bool popup_confirm_visible(void)
{
    return s_confirm.visible;
}
