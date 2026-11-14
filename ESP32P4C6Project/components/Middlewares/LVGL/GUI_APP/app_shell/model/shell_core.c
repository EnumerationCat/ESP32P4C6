/**
 * @file shell_core.c
 * @brief 命令行数据层(model): 输入队列、行编辑、历史、终端文本缓冲、命令执行
 *
 * 设计说明:
 *  - 输入: 自定义行编辑器(支持退格/左右/Ctrl-C/上下键历史), 不依赖esp_console。
 *          各输入源统一投递字节到队列, shell任务只从队列取, 不直接碰具体硬件。
 *  - 输出: shell_write() 同时写UART和终端文本缓冲, 显示层通过快照拉取。
 *  - 命令: 命令表在 command.c/h 中, 结构参考 GD32 bootloader。
 */

#include "shell_core.h"
#include "command.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "SHELL_CORE";

/**********************
 *      TYPEDEFS
 **********************/

/* 按键解析状态: 方向键等转义序列是逐字节到达的, 需记住解析到第几个字节 */
typedef enum
{
    WAIT_NORMAL = 0,        /* 普通状态, 字节按普通按键处理 */
    WAIT_SPEC_KEY,          /* 已收到ESC, 等 '[' 或 'O' */
    WAIT_FUNC_KEY,          /* 已确认序列前缀, 等 A/B/C/D 功能码 */
} shell_key_state_t;

/* 终端/命令行状态, shell任务与显示层共享(text/len/dirty/cursor_col 受 lock 保护) */
typedef struct
{
    SemaphoreHandle_t lock;                    /* 终端缓冲互斥锁 */
    char text[SHELL_BUF_SIZE + 1];             /* 终端文本缓冲(显示层快照来源) */
    size_t len;                                /* text 当前长度 */
    bool dirty;                                /* 有新内容待显示层拉取 */
    int cursor_col;                            /* 当前输入行光标显示列(含提示符) */
    char hist[SHELL_HIST_MAX][SHELL_LINE_MAX]; /* 命令历史 */
    int hist_count;                            /* 历史条数 */
    int hist_pos;                              /* 当前浏览位置(==hist_count表示新行) */
    volatile bool prompt_req;                  /* 请求shell任务在串口补打一次提示符 */
} shell_console_t;

/**********************
 *   STATIC VARIABLES
 **********************/
static shell_console_t s_console;
static QueueHandle_t s_key_q;      /* 输入字节队列: 各输入源 -> shell任务 */
static bool s_started;
static char s_cwd[SHELL_CWD_MAX] = SHELL_CWD_HOME;   /* 当前目录(受 s_console.lock 保护) */

/**********************
 *   当前目录与提示符
 **********************/

/* 组装 "<cwd>> "(调用者需持有 s_console.lock) */
static void prompt_build_locked(char *buf, size_t cap)
{
    snprintf(buf, cap, "%s> ", s_cwd);
}

void shell_core_set_cwd(const char *path)
{
    if (path == NULL || path[0] != '/' || s_console.lock == NULL)
    {
        return;
    }

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    snprintf(s_cwd, sizeof(s_cwd), "%s", path);
    xSemaphoreGive(s_console.lock);
}

size_t shell_core_get_cwd(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0)
    {
        return 0;
    }

    if (s_console.lock == NULL)
    {
        snprintf(buf, cap, "%s", SHELL_CWD_HOME);
        return strlen(buf);
    }

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    snprintf(buf, cap, "%s", s_cwd);
    xSemaphoreGive(s_console.lock);

    return strlen(buf);
}

void shell_core_prompt(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0)
    {
        return;
    }

    if (s_console.lock == NULL)
    {
        snprintf(buf, cap, "%s> ", SHELL_CWD_HOME);
        return;
    }

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    prompt_build_locked(buf, cap);
    xSemaphoreGive(s_console.lock);
}

/**********************
 *   UART / 文本缓冲输出
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

/* 同时输出到UART和终端文本缓冲 */
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

void shell_core_puts(const char *s)
{
    shell_write(s, strlen(s));
}

/* 打印当前提示符到UART和终端缓冲 */
static void shell_print_prompt(void)
{
    char prompt[SHELL_CWD_MAX + 4];

    shell_core_prompt(prompt, sizeof(prompt));
    shell_core_puts(prompt);
}

void shell_core_vprintf(const char *fmt, va_list ap)
{
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);

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

void shell_core_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    shell_core_vprintf(fmt, ap);
    va_end(ap);
}

void shell_core_clear(void)
{
    shell_raw("\x1b[2J\x1b[H");   /* 清屏并归位(UART终端) */

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    s_console.len = 0;
    s_console.text[0] = 0;
    s_console.cursor_col = 0;
    s_console.dirty = true;
    xSemaphoreGive(s_console.lock);
}

void shell_core_reset_to_prompt(void)
{
    char prompt[SHELL_CWD_MAX + 4];

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    s_console.len = 0;
    s_console.text[0] = 0;
    s_console.cursor_col = 0;
    prompt_build_locked(prompt, sizeof(prompt));
    for (const char *p = prompt; *p != 0; p++)
    {
        term_putc_locked(*p);
    }
    s_console.dirty = true;
    xSemaphoreGive(s_console.lock);
}

void shell_core_request_prompt(void)
{
    s_console.prompt_req = true;
}

bool shell_core_snapshot(char *buf, size_t cap, int *cursor_col)
{
    bool dirty = false;
    size_t n;

    if (buf == NULL || cap == 0 || s_console.lock == NULL)
    {
        return false;
    }

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    if (s_console.dirty)
    {
        n = s_console.len;
        if (n > cap - 1)
        {
            n = cap - 1;
        }
        memcpy(buf, s_console.text, n);
        buf[n] = 0;
        if (cursor_col != NULL)
        {
            *cursor_col = s_console.cursor_col;
        }
        s_console.dirty = false;
        dirty = true;
    }
    xSemaphoreGive(s_console.lock);

    return dirty;
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
        shell_core_printf("unknown command: %s (type 'help')\r\n", argv[0]);
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

/* 用整行替换终端缓冲中的当前输入行(调用者需持有 s_console.lock) */
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

/* 重画输入行: UART用ANSI转义序列, 终端缓冲整行替换; cursor 为光标在 text 中的位置 */
static void shell_redraw_line(const char *text, size_t cursor)
{
    char prompt[SHELL_CWD_MAX + 4];
    char uart_line[SHELL_LINE_MAX + SHELL_CWD_MAX + 32];
    char lcd_line[SHELL_LINE_MAX + SHELL_CWD_MAX + 8];
    size_t tlen = strlen(text);
    size_t plen;
    int n;

    if (cursor > tlen)
    {
        cursor = tlen;
    }

    shell_core_prompt(prompt, sizeof(prompt));
    plen = strlen(prompt);

    /* \r回到行首, 打印提示符和整行, \x1b[K 清掉行尾残留 */
    n = snprintf(uart_line, sizeof(uart_line), "\r%s%s\x1b[K", prompt, text);
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

    snprintf(lcd_line, sizeof(lcd_line), "%s%s", prompt, text);

    xSemaphoreTake(s_console.lock, portMAX_DELAY);
    term_set_current_line_locked(lcd_line);
    s_console.cursor_col = (int)(plen + cursor);   /* 光标所在的显示列 */
    xSemaphoreGive(s_console.lock);
}

/**********************
 *   输入队列
 **********************/

/* 投递一个输入字节; wait 为队列满时的等待时间(0表示不等待) */
static void shell_input_push(uint8_t ch, TickType_t wait)
{
    if (s_key_q == NULL)
    {
        return;
    }
    xQueueSend(s_key_q, &ch, wait);
}

void shell_core_input_push(uint8_t ch)
{
    /* 非阻塞: 供任意任务/回调使用, 队列满时丢弃该字节 */
    shell_input_push(ch, 0);
}

void shell_core_input_push_wait(uint8_t ch)
{
    /* 允许等待: 采集任务用, shell忙时把背压传回输入源 */
    shell_input_push(ch, portMAX_DELAY);
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

    shell_core_puts("\r\nESP32-P4 app_shell ready. Type 'help'.\r\n");
    shell_print_prompt();

    for (;;)
    {
        uint8_t ch;

        /* 100ms超时只用来处理 prompt_req, 按键字节全部来自队列 */
        if (xQueueReceive(s_key_q, &ch, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            /* 显示层进入页面: 只在串口补打一次提示符 */
            if (s_console.prompt_req)
            {
                s_console.prompt_req = false;
                if (len == 0)
                {
                    char prompt[SHELL_CWD_MAX + 4];

                    shell_core_prompt(prompt, sizeof(prompt));
                    shell_raw("\r\n");
                    shell_raw(prompt);
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

            shell_core_puts("\r\n");
            line[len] = 0;
            hist_push(line);
            shell_exec(line);
            len = 0;
            cursor = 0;
            line[0] = 0;                /* 清掉旧命令, 否则下次插入会把它搬回来 */
            s_console.hist_pos = s_console.hist_count;
            shell_print_prompt();
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
            shell_core_puts("^C\r\n");
            shell_print_prompt();
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
 *   GLOBAL FUNCTIONS
 **********************/
void shell_core_init(void)
{
    if (s_console.lock != NULL)
    {
        return;
    }

    s_console.lock = xSemaphoreCreateMutex();
    configASSERT(s_console.lock != NULL);

    s_key_q = xQueueCreate(SHELL_KEY_QUEUE_LEN, sizeof(uint8_t));
    configASSERT(s_key_q != NULL);
}

void shell_core_start(void)
{
    if (s_started)
    {
        return;
    }
    s_started = true;

    xTaskCreate(shell_task, "app_shell", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "shell task started");
}
