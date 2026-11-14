/**
 * @file command.h
 * @brief app_shell 命令表(参考 GD32 bootloader 的 command.c/h 结构)
 */

#ifndef __COMMAND_H__
#define __COMMAND_H__

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

struct command
{
    const char *name;                       /* 命令名 */
    const char *short_help;                 /* help 列表里显示的一行说明 */
    const char *long_help;                  /* 详细说明(可留空) */
    int (*function)(int argc, char **argv); /* 命令处理函数 */
};

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief       获取命令表(以NULL结尾)
 * @param       无
 * @retval      命令指针数组
 */
struct command **get_cmds(void);

/**
 * @brief       按名字查找命令
 * @param       name: 命令名
 * @retval      找到返回命令指针, 否则返回NULL
 */
struct command *find_cmd(const char *name);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*__COMMAND_H__*/
