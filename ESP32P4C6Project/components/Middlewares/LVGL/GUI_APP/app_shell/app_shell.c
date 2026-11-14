/**
 * @file app_shell.c
 * @brief 串口命令行(UART0 115200)，输出同时镜像到 LCD 文本页
 *
 * 设计说明:
 *  - 输入: 自定义UART行编辑器(支持退格/Ctrl-C/上下键历史), 不依赖esp_console,
 *          这样提示符、回显和命令输出都能完整镜像到LCD。
 *  - 输出: shell_write() 同时写UART和RAM终端缓冲; LVGL任务通过 app_shell_tick()
 *          把缓冲刷到lv_label上, 保证所有LVGL调用都在LVGL任务里完成。
 *  - 命令: 命令表在 command.c/h 中, 结构参考 GD32 bootloader。
 */

#include "app_shell.h"
#include "command.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "APP_SHELL";

/**********************
 *   STATIC VARIABLES
 **********************/
static shell_console_t s_console;
static shell_ui_t s_ui;

/**********************
 *   UART / 镜像输出
 **********************/

/* 仅写UART, 不镜像(用于ANSI控制字符) */
static void shell_raw(const char *s)
{
    uart_write_bytes(SHELL_UART_NUM, s, strlen(s));
}

/* 从缓冲头部删除一整行, 用于滚动 */
static void term_drop_first_line_locked(void)
{
    char *nl = memchr(s_console.text, '\n', s_console.len);

    if (nl == NULL)
    {
        s_console.len = 0;
        s_console.text[0] = 0;
        s_console.cursor_col = 0;
        return;
    }

    size_t drop = (size_t)(nl - s_console.text) + 1;
    memmove(s_console.text, s_console.text + drop, s_console.len - drop);
    s_console.len -= drop;
    s_console.text[s_console.len] = 0;
}

/* 终端字符写入(调用者需持有 s_console.lock) */
static void term_putc_locked(char c)
{
    if (c == '\r')
    {
        s_console.cursor_col = 0;
        return;
    }

    if (c == '\n')
    {
        if (s_console.len >= SHELL_BUF_SIZE)
        {
            term_drop_first_line_locked();
        }
        s_console.text[s_console.len++] = '\n';
        s_console.text[s_console.len] = 0;
        s_console.cursor_col = 0;
        s_console.dirty = true;
        return;
    }

    if (c == '\b')
    {
        if (s_console.cursor_col > 0 && s_console.len > 0)
        {
            s_console.len--;
            s_console.text[s_console.len] = 0;
            s_console.cursor_col--;
            s_console.dirty = true;
        }
        return;
    }

    if ((unsigned char)c < 0x20 || c == 0x7f)
    {
        return;
    }

    if (s_console.len >= SHELL_BUF_SIZE)
    {
        term_drop_first_line_locked();
    }
    s_console.text[s_console.len++] = c;
    s_console.text[s_console.len] = 0;
    s_console.cursor_col++;
    s_console.dirty = true;
}

/* 同时输出到UART和LCD镜像缓冲 */
static void shell_write(const char *s, size_t n)
{
    if (n == 0)
    {
        return;
    }

    uart_write_bytes(SHELL_UART_NUM, s, n);

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    for (size_t i = 0; i < n; i++)
    {
        term_putc_locked(s[i]);
    }
    s_console.dirty = true;
    xSemaphoreGive(s_console.lock);
}

void app_shell_puts(const char *s)
{
    shell_write(s, strlen(s));
}

void app_shell_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        return;
    }
    if (n > (int)sizeof(buf) - 1)
    {
        n = (int)sizeof(buf) - 1;
    }
    shell_write(buf, (size_t)n);
}

void app_shell_clear(void)
{
    shell_raw("\x1b[2J\x1b[H");   /* 清屏并归位(UART终端) */

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    s_console.len = 0;
    s_console.text[0] = 0;
    s_console.cursor_col = 0;
    s_console.dirty = true;
    xSemaphoreGive(s_console.lock);
}

/* 只清LCD镜像并写一行提示符, 不动UART终端 */
static void shell_mirror_reset_to_prompt(void)
{
    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    s_console.len = 0;
    s_console.text[0] = 0;
    s_console.cursor_col = 0;
    for (const char *p = SHELL_PROMPT; *p != 0; p++)
    {
        term_putc_locked(*p);
    }
    s_console.dirty = true;
    xSemaphoreGive(s_console.lock);
}

/**********************
 *   命令管理
 **********************/

/* 解析并执行一行命令 */
static void shell_exec(char *line)
{
    char *argv[8];
    int argc = 0;
    char *save = NULL;

    for (char *tok = strtok_r(line, " \t", &save);
         tok != NULL && argc < (int)(sizeof(argv) / sizeof(argv[0]));
         tok = strtok_r(NULL, " \t", &save))
    {
        argv[argc++] = tok;
    }

    if (argc == 0)
    {
        return;
    }

    struct command *cmd = find_cmd(argv[0]);
    if (cmd == NULL)
    {
        app_shell_printf("unknown command: %s (type 'help')\r\n", argv[0]);
        return;
    }
    cmd->function(argc, argv);
}

/**********************
 *   命令历史
 **********************/

static void hist_push(const char *line)
{
    if (line[0] == 0)
    {
        return;
    }
    if (s_console.hist_count > 0 && strcmp(s_console.hist[s_console.hist_count - 1], line) == 0)
    {
        return;
    }
    if (s_console.hist_count == SHELL_HIST_MAX)
    {
        memmove(s_console.hist[0], s_console.hist[1], (SHELL_HIST_MAX - 1) * SHELL_LINE_MAX);
        s_console.hist_count--;
    }
    snprintf(s_console.hist[s_console.hist_count], SHELL_LINE_MAX, "%s", line);
    s_console.hist_count++;
}

/* 用整行替换LCD镜像中的当前输入行(调用者需持有 s_console.lock) */
static void term_set_current_line_locked(const char *text)
{
    size_t start = s_console.len;

    while (start > 0 && s_console.text[start - 1] != '\n')
    {
        start--;
    }
    s_console.len = start;
    s_console.text[s_console.len] = 0;
    s_console.cursor_col = 0;
    s_console.dirty = true;

    while (*text != 0)
    {
        term_putc_locked(*text++);
    }
}

/* 重画输入行: UART用ANSI转义序列, LCD镜像整行替换; cursor 为光标在 text 中的位置 */
static void shell_redraw_line(const char *text, size_t cursor)
{
    char uart_line[SHELL_LINE_MAX + 48];
    char lcd_line[SHELL_LINE_MAX + 16];
    size_t tlen = strlen(text);
    int n;

    if (cursor > tlen)
    {
        cursor = tlen;
    }

    /* \r回到行首, 打印提示符和整行, \x1b[K 清掉行尾残留 */
    n = snprintf(uart_line, sizeof(uart_line), "\r" SHELL_PROMPT "%s\x1b[K", text);
    if (n > 0)
    {
        if (n > (int)sizeof(uart_line) - 1)
        {
            n = (int)sizeof(uart_line) - 1;
        }
        uart_write_bytes(SHELL_UART_NUM, uart_line, (size_t)n);

        /* 光标不在行尾时左移到位 */
        if (cursor < tlen)
        {
            n = snprintf(uart_line, sizeof(uart_line), "\x1b[%uD", (unsigned)(tlen - cursor));
            if (n > 0)
            {
                if (n > (int)sizeof(uart_line) - 1)
                {
                    n = (int)sizeof(uart_line) - 1;
                }
                uart_write_bytes(SHELL_UART_NUM, uart_line, (size_t)n);
            }
        }
    }

    snprintf(lcd_line, sizeof(lcd_line), SHELL_PROMPT "%s", text);

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    term_set_current_line_locked(lcd_line);
    s_console.cursor_col = (int)(strlen(SHELL_PROMPT) + cursor);   /* 光标所在的显示列 */
    xSemaphoreGive(s_console.lock);
}

/**********************
 *   shell 任务
 **********************/
static void shell_task(void *arg)
{
    (void)arg;

    char line[SHELL_LINE_MAX];
    size_t len = 0;
    size_t cursor = 0;      /* 光标在 line 中的位置, 0..len */
    shell_key_state_t key_state = WAIT_NORMAL;
    bool last_cr = false;

    /* line 必须始终以'\0'结尾(插入/重画都依赖这个不变量), 任务栈初始内容不可信 */
    line[0] = 0;

    app_shell_puts("\r\nESP32-P4 app_shell ready. Type 'help'.\r\n");
    app_shell_puts(SHELL_PROMPT);

    for (;;)
    {
        uint8_t ch;
        int n = uart_read_bytes(SHELL_UART_NUM, &ch, 1, pdMS_TO_TICKS(100));

        if (n != 1)
        {
            /* 进入LCD文本页: 只在串口补打一次提示符, LCD已由shell_mirror_reset_to_prompt处理 */
            if (s_console.prompt_req)
            {
                s_console.prompt_req = false;
                if (len == 0)
                {
                    shell_raw("\r\n" SHELL_PROMPT);
                }
            }
            continue;
        }

        s_console.prompt_req = false;

        /* 方向键: ESC [ A/B/C/D, 也接受应用光标模式的 ESC O A/B */
        if (key_state == WAIT_SPEC_KEY)
        {
            if (ch == '[' || ch == 'O')
            {
                key_state = WAIT_FUNC_KEY;
                continue;
            }
            /* 不是转义序列: 退回普通状态, 该字节按普通键继续处理 */
            key_state = WAIT_NORMAL;
        }
        else if (key_state == WAIT_FUNC_KEY)
        {
            if (ch < 0x40 || ch > 0x7e)     /* 参数字节(如Delete键的'3'), 继续等终止字节 */
            {
                continue;
            }
            key_state = WAIT_NORMAL;

            if (ch == 'A')                  /* 上: 更早的历史 */
            {
                if (s_console.hist_pos > 0)
                {
                    s_console.hist_pos--;
                    snprintf(line, sizeof(line), "%s", s_console.hist[s_console.hist_pos]);
                    len = strlen(line);
                    cursor = len;
                    shell_redraw_line(line, cursor);
                }
            }
            else if (ch == 'B')             /* 下: 更新的历史 */
            {
                if (s_console.hist_pos < s_console.hist_count)
                {
                    s_console.hist_pos++;
                    if (s_console.hist_pos == s_console.hist_count)
                    {
                        line[0] = 0;
                    }
                    else
                    {
                        snprintf(line, sizeof(line), "%s", s_console.hist[s_console.hist_pos]);
                    }
                    len = strlen(line);
                    cursor = len;
                    shell_redraw_line(line, cursor);
                }
            }
            else if (ch == 'C')             /* 右 */
            {
                if (cursor < len)
                {
                    cursor++;
                    shell_redraw_line(line, cursor);
                }
            }
            else if (ch == 'D')             /* 左 */
            {
                if (cursor > 0)
                {
                    cursor--;
                    shell_redraw_line(line, cursor);
                }
            }
            continue;
        }
        if (ch == 0x1b)
        {
            key_state = WAIT_SPEC_KEY;
            continue;
        }

        /* 回车: CRLF和单独LF都接受 */
        if (ch == '\r' || ch == '\n')
        {
            if (ch == '\n' && last_cr)
            {
                last_cr = false;
                continue;
            }
            last_cr = (ch == '\r');

            app_shell_puts("\r\n");
            line[len] = 0;
            hist_push(line);
            shell_exec(line);
            len = 0;
            cursor = 0;
            line[0] = 0;                /* 清掉旧命令, 否则下次插入会把它搬回来 */
            s_console.hist_pos = s_console.hist_count;
            app_shell_puts(SHELL_PROMPT);
            continue;
        }
        last_cr = false;

        /* 退格: 删除光标前一个字符 */
        if (ch == 0x7f || ch == 0x08)
        {
            if (cursor > 0)
            {
                memmove(line + cursor - 1, line + cursor, len - cursor + 1);
                len--;
                cursor--;
                shell_redraw_line(line, cursor);
            }
            continue;
        }

        /* Ctrl-C */
        if (ch == 0x03)
        {
            len = 0;
            cursor = 0;
            line[0] = 0;
            s_console.hist_pos = s_console.hist_count;
            app_shell_puts("^C\r\n" SHELL_PROMPT);
            continue;
        }

        /* 可打印字符: 插入到光标处 */
        if (ch >= 0x20 && ch < 0x7f && len < SHELL_LINE_MAX - 1)
        {
            memmove(line + cursor + 1, line + cursor, len - cursor + 1);
            line[cursor] = (char)ch;
            len++;
            cursor++;

            if (cursor == len)          /* 在行尾输入: 直接回显, 不必重画整行 */
            {
                shell_write((const char *)&ch, 1);
            }
            else
            {
                shell_redraw_line(line, cursor);
            }
        }
    }
}

/**********************
 *   LVGL 界面
 **********************/
static void shell_home_btn_cb(lv_event_t *e)
{
    (void)e;

    if (s_ui.home_screen != NULL)
    {
        lv_scr_load(s_ui.home_screen);
    }
}

/* 创建LCD文本页(只在LVGL任务中调用) */
static void app_shell_ui_create(void)
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

    /* 输入光标: 2px竖条, 作为label的子对象, 跟随文本一起滚动
     * 位置由 shell_cursor_update() 用 lv_label_get_letter_pos() 计算 */
    s_ui.cursor_obj = lv_obj_create(s_ui.term_label);
    lv_obj_remove_style_all(s_ui.cursor_obj);
    lv_obj_set_size(s_ui.cursor_obj, 2, lv_font_get_line_height(&lv_font_montserrat_12));
    lv_obj_set_style_bg_color(s_ui.cursor_obj, lv_color_hex(0x33FF33), 0);
    lv_obj_set_style_bg_opa(s_ui.cursor_obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.cursor_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ui.cursor_obj, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "LCD console created");
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void app_shell_init(void)
{
    if (s_console.lock != NULL)
    {
        return;
    }

    s_console.lock = xSemaphoreCreateMutex();
    configASSERT(s_console.lock != NULL);

    const uart_config_t cfg =
    {
        .baud_rate = SHELL_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(SHELL_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_driver_install(SHELL_UART_NUM, 2048, 0, 0, NULL, 0));

    xTaskCreate(shell_task, "app_shell", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "shell started on UART0, 115200");
}

/* 把光标竖条定位到输入行的当前光标处(只在LVGL任务中调用)
 * col: 镜像文本中当前输入行的显示列(含提示符), 已在锁内快照 */
static void shell_cursor_update(int col)
{
    const char *txt = s_console.tick_buf;
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

/* 光标闪烁(只在LVGL任务中调用) */
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

/* 把RAM缓冲刷到LCD标签(只在LVGL任务中调用) */
static void shell_label_refresh(void)
{
    bool dirty = false;
    int cursor_col = 0;

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    if (s_console.dirty)
    {
        memcpy(s_console.tick_buf, s_console.text, s_console.len + 1);
        cursor_col = s_console.cursor_col;
        s_console.dirty = false;
        dirty = true;
    }
    xSemaphoreGive(s_console.lock);

    if (!dirty || s_ui.term_label == NULL)
    {
        return;
    }

    lv_label_set_text(s_ui.term_label, s_console.tick_buf);
    lv_obj_update_layout(s_ui.term_cont);

    shell_cursor_update(cursor_col);

    /* 滚到最新一行 */
    lv_coord_t y = lv_obj_get_height(s_ui.term_label) - lv_obj_get_content_height(s_ui.term_cont);
    lv_obj_scroll_to_y(s_ui.term_cont, y > 0 ? y : 0, LV_ANIM_OFF);
}

void app_shell_tick(void)
{
    app_shell_ui_create();
    shell_label_refresh();
    shell_cursor_blink();
}

void app_shell_open(void)
{
    app_shell_ui_create();

    if (lv_scr_act() != s_ui.screen)
    {
        if (s_ui.home_screen == NULL)
        {
            s_ui.home_screen = lv_scr_act();
        }

        /* 每次进入页面都从干净的一行提示符开始, 不重复累积 */
        shell_mirror_reset_to_prompt();
        shell_label_refresh();

        lv_scr_load(s_ui.screen);
        lv_obj_invalidate(s_ui.screen);

        /* 让shell任务在串口也补打一行提示符, 与LCD上的一行对齐 */
        s_console.prompt_req = true;
    }
}
