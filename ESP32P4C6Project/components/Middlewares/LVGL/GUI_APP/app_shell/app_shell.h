/**
 * @file app_shell.h
 * @brief 串口命令行(UART0 115200)，输出同时镜像到 LCD 文本页
 */

#ifndef APP_SHELL_H
#define APP_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>

/*********************
 *      DEFINES
 *********************/
#define SHELL_UART_NUM       UART_NUM_0
#define SHELL_UART_BAUD      115200
#define SHELL_PROMPT         "app_shell> "
#define SHELL_LINE_MAX       128
#define SHELL_BUF_SIZE       4096      /* LCD终端缓冲(字节) */
#define SHELL_HIST_MAX       16        /* 命令历史条数 */

/* LCD 顶栏高度 */
#define SHELL_BAR_H          36

/* LCD 输入光标闪烁周期(ms) */
#define SHELL_CURSOR_BLINK_MS  500

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

/* 终端/命令行状态, shell任务与LVGL任务共享(text/len/dirty/cursor_col 受 lock 保护) */
typedef struct
{
    SemaphoreHandle_t lock;                    /* 终端缓冲互斥锁 */
    char text[SHELL_BUF_SIZE + 1];             /* LCD镜像终端缓冲 */
    size_t len;                                /* text 当前长度 */
    bool dirty;                                /* 有新内容待刷到LCD */
    int cursor_col;                            /* 当前输入行光标显示列(含提示符, 用于LCD光标定位) */
    char hist[SHELL_HIST_MAX][SHELL_LINE_MAX]; /* 命令历史 */
    int hist_count;                            /* 历史条数 */
    int hist_pos;                              /* 当前浏览位置(==hist_count表示新行) */
    char tick_buf[SHELL_BUF_SIZE + 1];         /* LVGL任务刷新时的拷贝缓冲 */
    volatile bool prompt_req;                  /* 请求shell任务在串口补打一次提示符 */
} shell_console_t;

/* LCD界面对象, 只在LVGL任务中访问 */
typedef struct
{
    lv_obj_t *screen;                          /* LCD文本页 */
    lv_obj_t *term_cont;                       /* 可滚动终端区 */
    lv_obj_t *term_label;                      /* 文本标签 */
    lv_obj_t *cursor_obj;                      /* 输入光标竖条(term_label的子对象) */
    lv_obj_t *home_screen;                     /* 进入shell前的屏幕 */
	
} shell_ui_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief       初始化app_shell: 安装UART0驱动并启动命令行任务
 * @note        不调用LVGL接口, 可在lv_init()之前调用
 * @param       无
 * @retval      无
 */
void app_shell_init(void);

/**
 * @brief       刷新LCD镜像文本
 * @note        必须在LVGL任务上下文中周期调用(与lv_timer_handler同一任务)
 * @param       无
 * @retval      无
 */
void app_shell_tick(void);

/**
 * @brief       切换到LCD文本页
 * @note        只能在LVGL任务上下文中调用(例如按钮回调)
 * @param       无
 * @retval      无
 */
void app_shell_open(void);

/**
 * @brief       向UART和LCD镜像输出字符串(供命令实现使用)
 * @param       s: 以'\0'结尾的字符串
 * @retval      无
 */
void app_shell_puts(const char *s);

/**
 * @brief       格式化输出到UART和LCD镜像(供命令实现使用)
 * @param       fmt: 格式字符串
 * @retval      无
 */
void app_shell_printf(const char *fmt, ...);

/**
 * @brief       清空UART终端和LCD镜像(供命令实现使用)
 * @param       无
 * @retval      无
 */
void app_shell_clear(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*APP_SHELL_H*/
