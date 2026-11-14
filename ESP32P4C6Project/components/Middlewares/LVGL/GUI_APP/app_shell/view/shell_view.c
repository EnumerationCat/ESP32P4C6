/**
 * @file shell_view.c
 * @brief 命令行显示与交互层(view/presenter): LCD文本页、终端文本标签、输入光标
 *
 * 页面通过 shell_ui_t 中的操作表暴露生命周期回调:
 *   init/deinit         创建/销毁控件
 *   on_start/on_pause   进入/离开页面
 *   on_update           周期刷新(presenter: 拉model快照 -> view)
 *   on_stop             停止页面
 *   on_event            事件分发(按eventID字符串比较)
 *   on_key              按键输入 -> model
 *
 * 本文件只在LVGL任务中运行; 数据全部来自 shell_core 的快照。
 */

#include "shell_view.h"
#include "shell_core.h"
#include "lvgl.h"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "SHELL_VIEW";

/**********************
 *      TYPEDEFS
 **********************/

/* 页面操作表 + LCD界面对象, 只在LVGL任务中访问 */
typedef struct
{
    /* 生命周期/交互回调 */
    void (*init)(void);                     /* 创建控件 */
    void (*deinit)(void);                   /* 销毁控件 */
    void (*on_start)(void);                 /* 进入页面 */
    void (*on_pause)(void);                 /* 离开页面(失去焦点) */
    void (*on_update)(void);                /* 周期刷新 */
    void (*on_stop)(void);                  /* 停止页面 */
    void (*on_event)(const char *event_id); /* 事件分发(字符串ID) */
    void (*on_key)(uint8_t ch);             /* 按键输入 -> model */

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
static void shell_init(void);
static void shell_deinit(void);
static void shell_on_start(void);
static void shell_on_pause(void);
static void shell_on_update(void);
static void shell_on_stop(void);
static void shell_on_event(const char *event_id);
static void shell_on_key(uint8_t ch);

/**********************
 *   STATIC VARIABLES
 **********************/
static shell_ui_t s_ui =
{
    .init      = shell_init,
    .deinit    = shell_deinit,
    .on_start  = shell_on_start,
    .on_pause  = shell_on_pause,
    .on_update = shell_on_update,
    .on_stop   = shell_on_stop,
    .on_event  = shell_on_event,
    .on_key    = shell_on_key,
};

static char s_tick_buf[SHELL_BUF_SIZE + 1];    /* 终端文本快照缓冲 */

/**********************
 *   LVGL 控件操作
 **********************/

/* HOME按钮: 统一走事件分发 */
static void shell_home_btn_cb(lv_event_t *e)
{
    (void)e;

    shell_on_event("home");
}

/* 把光标竖条定位到输入行的当前光标处
 * col: 快照文本中当前输入行的显示列(含提示符) */
static void shell_cursor_update(int col)
{
    const char *txt = s_tick_buf;
    const char *nl;
    size_t start;
    size_t len;
    size_t idx;
    lv_point_t pos;

    if (s_ui.cursor_obj == NULL)
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
    lv_label_get_letter_pos(s_ui.term_label, (uint32_t)idx, &pos);
    lv_obj_set_pos(s_ui.cursor_obj, pos.x, pos.y);
}

/* 光标闪烁 */
static void shell_cursor_blink(void)
{
    bool on;

    if (s_ui.cursor_obj == NULL)
    {
        return;
    }

    on = ((lv_tick_get() / SHELL_CURSOR_BLINK_MS) % 2) == 0;

    if (on == !lv_obj_has_flag(s_ui.cursor_obj, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }

    if (on)
    {
        lv_obj_clear_flag(s_ui.cursor_obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_ui.cursor_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 把model快照刷到LCD标签 */
static void shell_view_refresh(void)
{
    int cursor_col = 0;

    if (s_ui.term_label == NULL)
    {
        return;
    }

    if (!shell_core_snapshot(s_tick_buf, sizeof(s_tick_buf), &cursor_col))
    {
        return;
    }

    lv_label_set_text(s_ui.term_label, s_tick_buf);
    lv_obj_update_layout(s_ui.term_cont);

    shell_cursor_update(cursor_col);

    /* 滚到最新一行 */
    lv_coord_t y = lv_obj_get_height(s_ui.term_label) - lv_obj_get_content_height(s_ui.term_cont);
    lv_obj_scroll_to_y(s_ui.term_cont, y > 0 ? y : 0, LV_ANIM_OFF);
}

/* presenter: 拉model快照 -> view, 并驱动光标闪烁 */
static void shell_presenter_on_tick(void)
{
    shell_view_refresh();
    shell_cursor_blink();
}

/**********************
 *   生命周期/事件回调
 **********************/

/* 创建LCD文本页 */
static void shell_init(void)
{
    if (s_ui.screen != NULL)
    {
        return;
    }

    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, 0);

    /* 顶栏: 标题 + HOME按钮 */
    lv_obj_t *bar = lv_obj_create(s_ui.screen);
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
    lv_obj_add_event_cb(home, shell_home_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *home_label = lv_label_create(home);
    lv_label_set_text(home_label, "HOME");
    lv_obj_center(home_label);

    /* 终端显示区 */
    s_ui.term_cont = lv_obj_create(s_ui.screen);
    lv_obj_set_size(s_ui.term_cont, LV_HOR_RES, LV_VER_RES - SHELL_BAR_H);
    lv_obj_set_pos(s_ui.term_cont, 0, SHELL_BAR_H);
    lv_obj_set_style_radius(s_ui.term_cont, 0, 0);
    lv_obj_set_style_border_width(s_ui.term_cont, 0, 0);
    lv_obj_set_style_bg_color(s_ui.term_cont, lv_color_black(), 0);
    lv_obj_set_style_pad_all(s_ui.term_cont, 8, 0);
    lv_obj_set_scroll_dir(s_ui.term_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ui.term_cont, LV_SCROLLBAR_MODE_AUTO);

    s_ui.term_label = lv_label_create(s_ui.term_cont);
    lv_obj_set_width(s_ui.term_label, LV_HOR_RES - 16);
    lv_label_set_long_mode(s_ui.term_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_ui.term_label, lv_color_hex(0x33FF33), 0);
    lv_obj_set_style_text_font(s_ui.term_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_ui.term_label, "");

    /* 输入光标: 2px竖条, 作为label的子对象, 跟随文本一起滚动 */
    s_ui.cursor_obj = lv_obj_create(s_ui.term_label);
    lv_obj_remove_style_all(s_ui.cursor_obj);
    lv_obj_set_size(s_ui.cursor_obj, 2, lv_font_get_line_height(&lv_font_montserrat_12));
    lv_obj_set_style_bg_color(s_ui.cursor_obj, lv_color_hex(0x33FF33), 0);
    lv_obj_set_style_bg_opa(s_ui.cursor_obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.cursor_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ui.cursor_obj, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "LCD console created");
}

/* 销毁LCD文本页 */
static void shell_deinit(void)
{
    if (s_ui.screen == NULL)
    {
        return;
    }

    lv_obj_del(s_ui.screen);
    s_ui.screen = NULL;
    s_ui.term_cont = NULL;
    s_ui.term_label = NULL;
    s_ui.cursor_obj = NULL;
    /* home_screen 属于外部页面, 不在这里销毁 */
}

/* 进入页面: 重置终端文本为一行提示符, 并在串口同步提示符 */
static void shell_on_start(void)
{
    shell_init();

    if (lv_scr_act() == s_ui.screen)
    {
        return;
    }

    if (s_ui.home_screen == NULL)
    {
        s_ui.home_screen = lv_scr_act();
    }

    shell_core_reset_to_prompt();
    shell_view_refresh();

    lv_scr_load(s_ui.screen);
    lv_obj_invalidate(s_ui.screen);

    shell_core_request_prompt();
}

/* 离开页面(失去焦点) */
static void shell_on_pause(void)
{
    /* 页面隐藏后控件不再绘制, 光标闪烁无需额外处理 */
}

/* 周期刷新 */
static void shell_on_update(void)
{
    shell_init();
    shell_presenter_on_tick();
}

/* 停止页面 */
static void shell_on_stop(void)
{
    shell_deinit();
}

/* 事件分发: 按eventID字符串比较 */
static void shell_on_event(const char *event_id)
{
    if (event_id == NULL)
    {
        return;
    }

    if (strcmp(event_id, "home") == 0)
    {
        s_ui.on_pause();
        if (s_ui.home_screen != NULL)
        {
            lv_scr_load(s_ui.home_screen);
        }
    }
}

/* 按键输入 -> model */
static void shell_on_key(uint8_t ch)
{
    shell_core_input_push(ch);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void shell_view_tick(void)
{
    s_ui.on_update();
}

void shell_view_open(void)
{
    s_ui.on_start();
}
