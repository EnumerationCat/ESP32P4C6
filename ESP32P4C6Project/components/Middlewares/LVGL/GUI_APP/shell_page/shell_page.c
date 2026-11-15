/**
 * @file shell_page.c
 * @brief 命令行页面(shell_page): 对外接口 + LCD文本页显示与交互
 *
 * 显示部分: shell_ui_t 的第一个成员是 view_t base(继承Framework基类), 通过它暴露生命周期回调:
 *   init/deinit         创建/销毁控件
 *   on_start/on_pause   进入/离开页面
 *   on_update           周期刷新(presenter: 拉model快照 -> view)
 *   on_stop             停止页面
 *   on_event            事件分发(按eventID字符串比较)
 *
 * 回调第一个参数是 view_t *self(基类指针), 用 container_of 还原出 shell_ui_t。
 * 本文件只在LVGL任务中运行; 数据全部来自 shell_core 的快照。
 */

#include "shell_page.h"
#include "shell_core.h"
#include "shell_input_uart.h"
#include "view.h"
#include "page_manager.h"
#include "container_of.h"
#include "lvgl.h"

#include "esp_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

/*********************
 *      DEFINES
 *********************/
#define SHELL_BAR_H            36      /* LCD顶栏高度 */
#define SHELL_CURSOR_BLINK_MS  500     /* 光标闪烁周期(ms) */

static const char *TAG = "SHELL_PAGE";

/**********************
 *      TYPEDEFS
 **********************/

/* 派生视图: base必须是第一个成员(C式继承) + LCD界面对象, 只在LVGL任务中访问 */
typedef struct
{
    view_t base;                            /* Framework基类(必须放第一位) */

    /* LVGL对象 */
    lv_obj_t *screen;                       /* LCD文本页 */
    lv_obj_t *term_cont;                    /* 可滚动终端区 */
    lv_obj_t *term_label;                   /* 文本标签 */
    lv_obj_t *cursor_obj;                   /* 输入光标竖条(term_label的子对象) */
    lv_obj_t *home_screen;                  /* 进入shell前的屏幕(外部页面) */
} shell_ui_t;

/**********************
 *    STATIC PROTOTYPES
 **********************/
static void shell_init(view_t *self);
static void shell_deinit(view_t *self);
static void shell_on_start(view_t *self);
static void shell_on_pause(view_t *self);
static void shell_on_update(view_t *self);
static void shell_on_stop(view_t *self);
static void shell_on_event(view_t *self, const char *event_id);

/**********************
 *   STATIC VARIABLES
 **********************/
static shell_ui_t s_ui =
{
    .base =
    {
        .id        = SHELL_PAGE_ID,
        .name      = "shell",
        .init      = shell_init,
        .deinit    = shell_deinit,
        .on_start  = shell_on_start,
        .on_pause  = shell_on_pause,
        .on_update = shell_on_update,
        .on_stop   = shell_on_stop,
        .on_event  = shell_on_event,
    },
};

static char s_tick_buf[SHELL_BUF_SIZE + 1];    /* 终端文本快照缓冲 */

/**********************
 *   LVGL 控件操作
 **********************/

/* 由基类指针还原派生视图 */
static inline shell_ui_t *shell_ui(view_t *self)
{
    return container_of(self, shell_ui_t, base);
}

/* HOME按钮: 通过基类指针统一走事件分发 */
static void shell_home_btn_cb(lv_event_t *e)
{
    view_t *self = (view_t *)lv_event_get_user_data(e);

    if (self != NULL && self->on_event != NULL)
    {
        self->on_event(self, "home");
    }
}

/* 把光标竖条定位到输入行的当前光标处
 * col: 快照文本中当前输入行的显示列(含提示符) */
static void shell_cursor_update(shell_ui_t *ui, int col)
{
    const char *txt = s_tick_buf;
    const char *nl;
    size_t start;
    size_t len;
    size_t idx;
    lv_point_t pos;

    if (ui->cursor_obj == NULL)
    {
        return;
    }

    nl = strrchr(txt, '\n');
    start = (nl != NULL) ? (size_t)(nl - txt) + 1 : 0;
    len = strlen(txt);
    idx = start + (size_t)col;
    if (idx > len)
    {
        idx = len;
    }

    /* label文本坐标与子对象坐标同原点; 该函数内部会处理自动换行 */
    lv_label_get_letter_pos(ui->term_label, (uint32_t)idx, &pos);
    lv_obj_set_pos(ui->cursor_obj, pos.x, pos.y);
}

/* 光标闪烁 */
static void shell_cursor_blink(shell_ui_t *ui)
{
    bool on;

    if (ui->cursor_obj == NULL)
    {
        return;
    }

    on = ((lv_tick_get() / SHELL_CURSOR_BLINK_MS) % 2) == 0;

    if (on == !lv_obj_has_flag(ui->cursor_obj, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }

    if (on)
    {
        lv_obj_clear_flag(ui->cursor_obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(ui->cursor_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 把model快照刷到LCD标签 */
static void shell_page_refresh(shell_ui_t *ui)
{
    int cursor_col = 0;

    if (ui->term_label == NULL)
    {
        return;
    }

    if (!shell_core_snapshot(s_tick_buf, sizeof(s_tick_buf), &cursor_col))
    {
        return;
    }

    lv_label_set_text(ui->term_label, s_tick_buf);
    lv_obj_update_layout(ui->term_cont);

    shell_cursor_update(ui, cursor_col);

    /* 滚到最新一行 */
    lv_coord_t y = lv_obj_get_height(ui->term_label) - lv_obj_get_content_height(ui->term_cont);
    lv_obj_scroll_to_y(ui->term_cont, y > 0 ? y : 0, LV_ANIM_OFF);
}

/* presenter: 拉model快照 -> view, 并驱动光标闪烁 */
static void shell_presenter_on_tick(shell_ui_t *ui)
{
    shell_page_refresh(ui);
    shell_cursor_blink(ui);
}

/**********************
 *   生命周期/事件回调
 **********************/

/* 创建LCD文本页 */
static void shell_init(view_t *self)
{
    shell_ui_t *ui = shell_ui(self);

    if (ui->screen != NULL)
    {
        return;
    }

    ui->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui->screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ui->screen, LV_OPA_COVER, 0);

    /* 顶栏: 标题 + HOME按钮 */
    lv_obj_t *bar = lv_obj_create(ui->screen);
    lv_obj_set_size(bar, LV_HOR_RES, SHELL_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E1E1E), 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "SHELL  UART0 115200");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t *home = lv_btn_create(bar);
    lv_obj_set_size(home, 64, SHELL_BAR_H - 8);
    lv_obj_align(home, LV_ALIGN_RIGHT_MID, -4, 0);
    /* 把基类指针传给按钮, 回调里即可直接分发事件 */
    lv_obj_add_event_cb(home, shell_home_btn_cb, LV_EVENT_CLICKED, self);

    lv_obj_t *home_label = lv_label_create(home);
    lv_label_set_text(home_label, "HOME");
    lv_obj_center(home_label);

    /* 终端显示区 */
    ui->term_cont = lv_obj_create(ui->screen);
    lv_obj_set_size(ui->term_cont, LV_HOR_RES, LV_VER_RES - SHELL_BAR_H);
    lv_obj_set_pos(ui->term_cont, 0, SHELL_BAR_H);
    lv_obj_set_style_radius(ui->term_cont, 0, 0);
    lv_obj_set_style_border_width(ui->term_cont, 0, 0);
    lv_obj_set_style_bg_color(ui->term_cont, lv_color_black(), 0);
    lv_obj_set_style_pad_all(ui->term_cont, 8, 0);
    lv_obj_set_scroll_dir(ui->term_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->term_cont, LV_SCROLLBAR_MODE_AUTO);

    ui->term_label = lv_label_create(ui->term_cont);
    lv_obj_set_width(ui->term_label, LV_HOR_RES - 16);
    lv_label_set_long_mode(ui->term_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(ui->term_label, lv_color_hex(0x33FF33), 0);
    lv_obj_set_style_text_font(ui->term_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(ui->term_label, "");

    /* 输入光标: 2px竖条, 作为label的子对象, 跟随文本一起滚动 */
    ui->cursor_obj = lv_obj_create(ui->term_label);
    lv_obj_remove_style_all(ui->cursor_obj);
    lv_obj_set_size(ui->cursor_obj, 2, lv_font_get_line_height(&lv_font_montserrat_12));
    lv_obj_set_style_bg_color(ui->cursor_obj, lv_color_hex(0x33FF33), 0);
    lv_obj_set_style_bg_opa(ui->cursor_obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui->cursor_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->cursor_obj, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "LCD console created");
}

/* 销毁LCD文本页 */
static void shell_deinit(view_t *self)
{
    shell_ui_t *ui = shell_ui(self);

    if (ui->screen == NULL)
    {
        return;
    }

    lv_obj_del(ui->screen);
    ui->screen = NULL;
    ui->term_cont = NULL;
    ui->term_label = NULL;
    ui->cursor_obj = NULL;
    /* home_screen 属于外部页面, 不在这里销毁 */
}

/* 进入页面: 重置终端文本为一行提示符, 并在串口同步提示符 */
static void shell_on_start(view_t *self)
{
    shell_ui_t *ui = shell_ui(self);

    shell_init(self);

    if (lv_scr_act() == ui->screen)
    {
        return;
    }

    if (ui->home_screen == NULL)
    {
        ui->home_screen = lv_scr_act();
    }

    shell_core_reset_to_prompt();
    shell_page_refresh(ui);

    lv_scr_load(ui->screen);
    lv_obj_invalidate(ui->screen);

    shell_core_request_prompt();
}

/* 离开页面(失去焦点) */
static void shell_on_pause(view_t *self)
{
    (void)self;

    /* 页面隐藏后控件不再绘制, 光标闪烁无需额外处理 */
}

/* 周期刷新 */
static void shell_on_update(view_t *self)
{
    shell_ui_t *ui = shell_ui(self);

    shell_init(self);
    shell_presenter_on_tick(ui);
}

/* 停止页面 */
static void shell_on_stop(view_t *self)
{
    shell_deinit(self);
}

/* 事件分发: 按eventID字符串比较 */
static void shell_on_event(view_t *self, const char *event_id)
{
    shell_ui_t *ui = shell_ui(self);

    if (event_id == NULL)
    {
        return;
    }

    if (strcmp(event_id, "home") == 0)
    {
        /* 先切回主界面, 再关闭页面(关闭会删除shell屏幕, 不能删当前活动屏) */
        if (ui->home_screen != NULL)
        {
            lv_scr_load(ui->home_screen);
        }

        page_close(self);       /* 交给页面管理器: on_stop + 恢复挂起页 */
    }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/* 初始化: 创建数据层 -> 安装UART输入源 -> 启动shell任务 -> 注册到页面管理器 */
void shell_page_init(void)
{
    shell_core_init();
    shell_input_uart_init();
    shell_core_start();

    page_manager_init();
    page_register(&s_ui.base);
}

void shell_page_open(void)
{
    page_open(&s_ui.base);
}

void shell_page_puts(const char *s)
{
    shell_core_puts(s);
}

void shell_page_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    shell_core_vprintf(fmt, ap);
    va_end(ap);
}

void shell_page_clear(void)
{
    shell_core_clear();
}

void shell_page_input_push(uint8_t ch)
{
    shell_core_input_push(ch);
}
