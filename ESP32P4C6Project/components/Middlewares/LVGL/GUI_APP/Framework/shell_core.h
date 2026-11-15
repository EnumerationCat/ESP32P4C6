/**
 * @file shell_core.h
 * @brief 命令行数据层(model): 输入队列、行编辑、历史、终端文本缓冲、命令执行
 *
 * 分层约定:
 *  - 本层不依赖LVGL, 不调用任何lv_*接口;
 *  - 输入源(UART/USB键盘/软键盘等)统一调用 shell_core_input_push();
 *  - 显示层(view)通过 shell_core_snapshot() 拉取待刷新文本。
 */

#ifndef SHELL_CORE_H
#define SHELL_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/
#define SHELL_UART_NUM       UART_NUM_0
#define SHELL_UART_BAUD      115200
#define SHELL_CWD_HOME       "/User"   /* shell默认目录(SD卡挂载点) */
#define SHELL_CWD_MAX        192       /* 当前目录路径缓冲 */
#define SHELL_LINE_MAX       128
#define SHELL_BUF_SIZE       4096      /* 终端文本缓冲(字节) */
#define SHELL_HIST_MAX       16        /* 命令历史条数 */
#define SHELL_KEY_QUEUE_LEN  64        /* 输入字节队列深度 */

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief       创建互斥锁和输入队列
 * @note        不启动任务、不碰UART; 之后再调 shell_core_start()
 * @param       无
 * @retval      无
 */
void shell_core_init(void);

/**
 * @brief       启动shell任务(消费输入队列并执行命令)
 * @note        启动时会打印欢迎横幅(走UART输出), 需在UART驱动安装之后调用
 * @param       无
 * @retval      无
 */
void shell_core_start(void);

/**
 * @brief       向shell投递一个输入字节(所有输入源的统一入口)
 * @note        非阻塞, 可在任意任务/回调中调用; 队列满时丢弃该字节
 * @param       ch: 输入字节(可打印ASCII或ESC转义序列)
 * @retval      无
 */
void shell_core_input_push(uint8_t ch);

/**
 * @brief       投递输入字节, 队列满时阻塞等待
 * @note        供可承受背压的采集任务使用(如UART), 不能用于中断/回调
 * @param       ch: 输入字节
 * @retval      无
 */
void shell_core_input_push_wait(uint8_t ch);

/**
 * @brief       向UART和终端文本缓冲输出字符串(供命令实现使用)
 * @param       s: 以'\0'结尾的字符串
 * @retval      无
 */
void shell_core_puts(const char *s);

/**
 * @brief       格式化输出到UART和终端文本缓冲(供命令实现使用)
 * @param       fmt: 格式字符串
 * @retval      无
 */
void shell_core_printf(const char *fmt, ...);

/**
 * @brief       shell_core_printf 的 va_list 版本(供门面/其他层转发)
 * @param       fmt: 格式字符串
 * @param       ap: 参数列表
 * @retval      无
 */
void shell_core_vprintf(const char *fmt, va_list ap);

/**
 * @brief       清空UART终端和终端文本缓冲(供命令实现使用)
 * @param       无
 * @retval      无
 */
void shell_core_clear(void);

/**
 * @brief       拉取终端文本快照(显示层使用)
 * @note        内部加锁; 只在有变化时拷贝文本并清除dirty
 * @param       buf: 输出缓冲
 * @param       cap: 输出缓冲大小(含'\0')
 * @param       cursor_col: 当前输入行光标显示列(含提示符), 可为NULL
 * @retval      true: 有新内容; false: 无变化
 */
bool shell_core_snapshot(char *buf, size_t cap, int *cursor_col);

/**
 * @brief       把终端文本缓冲重置为干净的一行提示符(显示层进入页面时使用)
 * @note        只改文本缓冲, 不写UART
 * @param       无
 * @retval      无
 */
void shell_core_reset_to_prompt(void);

/**
 * @brief       请求shell任务在串口补打一次提示符(显示层进入页面时使用)
 * @param       无
 * @retval      无
 */
void shell_core_request_prompt(void);

/**
 * @brief       设置shell当前目录(提示符随之变化)
 * @note        必须是绝对VFS路径(以'/'开头); cd命令调用
 * @param       path: 绝对路径, 如 "/User/img"
 * @retval      无
 */
void shell_core_set_cwd(const char *path);

/**
 * @brief       拷贝shell当前目录
 * @param       buf: 输出缓冲
 * @param       cap: 输出缓冲大小(含'\0')
 * @retval      写入的字符串长度
 */
size_t shell_core_get_cwd(char *buf, size_t cap);

/**
 * @brief       组装当前提示符 "<cwd>> "
 * @param       buf: 输出缓冲
 * @param       cap: 输出缓冲大小(含'\0')
 * @retval      无
 */
void shell_core_prompt(char *buf, size_t cap);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*SHELL_CORE_H*/
