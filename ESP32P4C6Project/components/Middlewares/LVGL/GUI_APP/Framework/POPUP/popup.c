/**
 * @file popup.c
 * @brief 弹窗实现: 顶部滑入提示 + 底部双按钮确认
 *
 * 两个弹窗都是 view_t 派生对象(与页面相同的C式继承), 通过 page_manager 的
 * page_popup_open()/page_popup_close() 管理生命周期:
 *   popup_open  -> 底层页 on_pause, 弹窗 on_start(创建控件并播放滑入动画)
 *   popup_close -> 弹窗 on_stop(删除控件), 全部弹窗关闭后底层页 on_resume
 *
 * 控件都挂在 lv_layer_top() 上, 因此不会替换当前页面的屏幕, 只覆盖显示;
 * 弹窗关闭时把控件删掉即可, 底层页面原样还在。
 * 本文件所有函数都必须在 LVGL 任务中调用。
 */

#include "popup.h"
#include "page_manager.h"
#include "view.h"
#include "container_of.h"
#include "lvgl.h"

#include <string.h>

#define POPUP_ID_NOTICE            100                 /* 仅供日志调试 */
#define POPUP_ID_CONFIRM           101

#define POPUP_ANIM_MS              250                 /* 滑入/滑出时长 */
#define POPUP_NOTICE_TEXT_MAX      64
#define POPUP_CONFIRM_DESC_MAX     128
#define POPUP_BTN_TEXT_MAX         24

#define POPUP_NOTICE_W             280                 /* 提示弹窗尺寸/位置 */
#define POPUP_NOTICE_H             96
#define POPUP_NOTICE_MARGIN_X      ((LV_HOR_RES - POPUP_NOTICE_W) / 2)
#define POPUP_NOTICE_REST_Y        56

#define POPUP_CONFIRM_W            300                 /* 确认弹窗尺寸/位置 */
#define POPUP_CONFIRM_H            200
#define POPUP_CONFIRM_MARGIN_X     ((LV_HOR_RES - POPUP_CONFIRM_W) / 2)
#define POPUP_CONFIRM_REST_Y       (LV_VER_RES - POPUP_CONFIRM_H - 20)
#define POPUP_BTN_W                120                 /* 按钮尺寸/左右下边距 */
#define POPUP_BTN_H                44
#define POPUP_BTN_MARGIN           20

/**********************
 *   顶部提示弹窗
 **********************/

typedef struct
{
    view_t base;                                        /* Framework基类(必须放第一位) */
    lv_obj_t *panel;                                    /* 提示面板 */
    lv_obj_t *label;                                    /* 居中文字 */
    lv_timer_t *timer;                                  /* 自动收回定时器 */
    bool visible;                                       /* 正在显示(含滑出过程) */
    bool hiding;                                        /* 正在滑出 */
    char text[POPUP_NOTICE_TEXT_MAX];                   /* 待显示文字(on_start前先缓存) */
    uint32_t duration_ms;                               /* 自动收回时间, 0=常驻 */
} popup_notice_ui_t;

/**********************
 *   底部确认弹窗
 **********************/

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
static void notice_on_start(view_t *self);
static void notice_on_stop(view_t *self);
static void confirm_on_start(view_t *self);
static void confirm_on_stop(view_t *self);

static void notice_slide_in(popup_notice_ui_t *ui);
static void notice_start_timer(popup_notice_ui_t *ui);
static void notice_out_ready_cb(lv_anim_t *a);

static void confirm_slide_in(popup_confirm_ui_t *ui);
static void confirm_out_ready_cb(lv_anim_t *a);
static void confirm_btn_event_cb(lv_event_t *e);

/**********************
 *   STATIC VARIABLES
 **********************/
static popup_notice_ui_t s_notice =
{
    .base =
    {
        .id       = POPUP_ID_NOTICE,
        .name     = "popup_notice",
        .on_start = notice_on_start,
        .on_stop  = notice_on_stop,
    },
};

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
static inline popup_notice_ui_t *notice_ui(view_t *self)
{
    return container_of(self, popup_notice_ui_t, base);
}

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

/**********************
 *   顶部提示弹窗
 **********************/

static void notice_out_ready_cb(lv_anim_t *a)
{
    (void)a;
    page_popup_close(&s_notice.base);
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
    lv_obj_set_y(ui->panel, -POPUP_NOTICE_H);
    popup_slide_y(ui->panel, -POPUP_NOTICE_H, POPUP_NOTICE_REST_Y, NULL);
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

    ui->panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ui->panel, POPUP_NOTICE_W, POPUP_NOTICE_H);
    lv_obj_set_style_radius(ui->panel, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->panel, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ui->panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui->panel, POPUP_NOTICE_MARGIN_X, -POPUP_NOTICE_H);

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

    page_popup_open(&s_notice.base, POPUP_PRIORITY_NOTICE);
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
                  -POPUP_NOTICE_H, notice_out_ready_cb);
}

bool popup_notice_visible(void)
{
    return s_notice.visible;
}

/**********************
 *   底部确认弹窗
 **********************/

static void confirm_out_ready_cb(lv_anim_t *a)
{
    (void)a;
    page_popup_close(&s_confirm.base);
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
    lv_obj_t *label;

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
    lv_obj_set_size(ui->panel, POPUP_CONFIRM_W, POPUP_CONFIRM_H);
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

    page_popup_open(&s_confirm.base, POPUP_PRIORITY_CONFIRM);
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
