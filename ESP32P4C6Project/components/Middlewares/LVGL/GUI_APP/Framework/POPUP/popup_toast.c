/**
 * @file popup_toast.c
 * @brief 屏幕中心提示弹窗实现: 系统级, 控件挂在 lv_layer_sys()
 *
 * 每次 popup_toast_show() 都新建一个独立实例(lv_mem_alloc), 多个实例
 * 在 popup_manager 的 ready 链表里按优先级排队/抢占, 出队后由 dispose 释放。
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

/* 屏幕中心提示弹窗(每次 show 创建一个实例) */
typedef struct popup_toast_ui
{
    popup_base_t base;                                  /* 弹窗基类(必须放第一位) */
    struct popup_toast_ui *next;                        /* 实例链表(hide/visible 遍历) */
    lv_obj_t *panel;                                    /* 提示面板(在屏幕中心) */
    lv_obj_t *label;                                    /* 居中文字 */
    bool visible;                                       /* 正在运行(显示中, 含淡出过程) */
    bool hiding;                                        /* 正在淡出 */
    char text[POPUP_TOAST_TEXT_MAX];                    /* 待显示文字(on_start前先缓存) */
} popup_toast_ui_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void toast_on_start(view_t *self);
static void toast_on_stop(view_t *self);
static void toast_fade_in(popup_toast_ui_t *ui);
static bool toast_hide_instance(popup_toast_ui_t *ui);
static bool toast_request_close(view_t *self);
static void toast_dispose(view_t *self);
static void toast_out_ready_cb(lv_anim_t *a);

/**********************
 *  STATIC VARIABLES
 **********************/
/* 实例模板: show 时拷贝一份再填数据 */
static const popup_toast_ui_t s_toast_template =
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
        .level         = POPUP_LEVEL_SYS,
        .priority      = SYSTEM_PRIORITY1,
        .value         = 0,
        .request_close = toast_request_close,
        .dispose       = toast_dispose,
    },
};

static popup_toast_ui_t *s_instances;                   /* 存活实例链表头 */

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

/* 透明度动画, ready/user_data可为NULL */
static void popup_fade(lv_obj_t *obj, lv_opa_t from, lv_opa_t to,
                       lv_anim_ready_cb_t ready, void *user_data)
{
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, popup_opa_anim_cb);
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

static void toast_out_ready_cb(lv_anim_t *a)
{
    popup_toast_ui_t *ui = lv_anim_get_user_data(a);

    if (ui != NULL)
    {
        g_popup_manager.destroy(&ui->base.view);
    }
}

/* 到期退出: 由 popup_manager 调用, 开始淡出动画; 返回是否已开始退场 */
static bool toast_request_close(view_t *self)
{
    return toast_hide_instance(toast_ui(self));
}

/* 以完全透明起步, 淡入到不透明 */
static void toast_fade_in(popup_toast_ui_t *ui)
{
    lv_anim_del(ui->panel, popup_opa_anim_cb);
    lv_obj_set_style_opa(ui->panel, LV_OPA_TRANSP, LV_PART_MAIN);
    popup_fade(ui->panel, LV_OPA_TRANSP, LV_OPA_COVER, NULL, NULL);
    ui->hiding = false;
}

/* 收回一个实例: 运行中则淡出(返回true), 排队中则直接出队释放(返回false) */
static bool toast_hide_instance(popup_toast_ui_t *ui)
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

    lv_anim_del(ui->panel, popup_opa_anim_cb);
    popup_fade(ui->panel,
               lv_obj_get_style_opa(ui->panel, LV_PART_MAIN),
               LV_OPA_TRANSP, toast_out_ready_cb, ui);

    return true;
}

/* 创建提示弹窗控件(开始运行时调用) */
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
}

/* 删除提示弹窗控件(实例内存由 dispose 释放) */
static void toast_on_stop(view_t *self)
{
    popup_toast_ui_t *ui = toast_ui(self);

    if (ui->panel != NULL)
    {
        lv_obj_del(ui->panel);          /* lv_obj_del 会同时删除该对象上的动画 */
        ui->panel = NULL;
    }

    ui->label   = NULL;
    ui->visible = false;
    ui->hiding  = false;
}

/* 出队后由管理器清理: 只从本类型实例链表摘除, 内存由 popup_dispose 统一释放 */
static void toast_dispose(view_t *self)
{
    popup_toast_ui_t *ui = toast_ui(self);
    popup_toast_ui_t **pp = &s_instances;

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

void popup_toast_show(const char *text, uint32_t duration_ms)
{
    popup_toast_ui_t proto = s_toast_template;      /* 先填好数据再上架 */
    popup_toast_ui_t *ui;

    if (text == NULL)
    {
        return;
    }

    strlcpy(proto.text, text, sizeof(proto.text));
    proto.base.value = duration_ms;                 /* 存活时间, 0=常驻 */

    ui = (popup_toast_ui_t *)g_popup_manager.create(
             &proto.base, sizeof(proto), POPUP_LEVEL_SYS, SYSTEM_PRIORITY1);

    if (ui == NULL)
    {
        return;
    }

    ui->next    = s_instances;                      /* 登记到本类型实例链表 */
    s_instances = ui;
}

void popup_toast_hide(void)
{
    popup_toast_ui_t *ui = s_instances;

    while (ui != NULL)
    {
        popup_toast_ui_t *next = ui->next;      /* 排队实例可能被立即释放 */

        toast_hide_instance(ui);
        ui = next;
    }
}

bool popup_toast_visible(void)
{
    popup_toast_ui_t *ui;

    for (ui = s_instances; ui != NULL; ui = ui->next)
    {
        if (ui->visible)
        {
            return true;
        }
    }

    return false;
}
