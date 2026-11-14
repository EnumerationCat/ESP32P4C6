/**
 * @file command.c
 * @brief app_shell 命令实现与命令表(参考 GD32 bootloader 的 command.c)
 */

#include "command.h"
#include "shell_core.h"
#include "lcd.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/**********************
 *   路径状态
 **********************/

#define CMD_PATH_MAX    SHELL_CWD_MAX                       /* 规范化后的绝对路径缓冲 */
#define CMD_CHILD_MAX   (CMD_PATH_MAX + 256 + 1)            /* stat用: 目录 + '/' + 文件名 */

/**
 * @brief       把用户输入解析成规范化绝对VFS路径
 * @note        支持相对路径、'.'/'..'、绝对路径, 以及LVGL盘符 D: -> /User、C: -> /Factory
 * @param       in: 用户输入路径
 * @param       out: 输出缓冲
 * @param       cap: 输出缓冲大小
 * @retval      true: 解析成功
 */
static bool path_resolve(const char *in, char *out, size_t cap)
{
    char work[CMD_PATH_MAX];
    char norm[CMD_PATH_MAX];
    const char *p;
    size_t nlen = 0;
    int n;

    if (in == NULL || in[0] == '\0')
    {
        in = ".";
    }

    /* 盘符写法: D、D:、D:/、D:dir、D:/dir 都映射到对应挂载点 */
    const char *root = NULL;

    if ((in[0] == 'D' || in[0] == 'd') && (in[1] == '\0' || in[1] == ':' || in[1] == '/'))
    {
        root = "/User";
    }
    else if ((in[0] == 'C' || in[0] == 'c') && (in[1] == '\0' || in[1] == ':' || in[1] == '/'))
    {
        root = "/Factory";
    }

    if (root != NULL)
    {
        const char *rest = in + 1;         /* 跳过盘符字母 */

        if (*rest == ':')
        {
            rest++;
        }

        if (rest[0] == '\0')
        {
            n = snprintf(work, sizeof(work), "%s", root);
        }
        else if (rest[0] == '/')
        {
            n = snprintf(work, sizeof(work), "%s%s", root, rest);
        }
        else
        {
            n = snprintf(work, sizeof(work), "%s/%s", root, rest);
        }
    }
    else if (in[0] == '/')                  /* 绝对路径 */
    {
        n = snprintf(work, sizeof(work), "%s", in);
    }
    else                                    /* 相对当前目录 */
    {
        char cwd[CMD_PATH_MAX];

        shell_core_get_cwd(cwd, sizeof(cwd));
        n = snprintf(work, sizeof(work), "%s/%s", cwd, in);
    }

    if (n < 0 || (size_t)n >= sizeof(work))
    {
        return false;                       /* 路径过长 */
    }

    /* 逐段规范化: 忽略空段和'.', '..'回退上一级 */
    p = work;
    while (*p != '\0')
    {
        const char *slash = strchr(p, '/');
        size_t seglen = (slash != NULL) ? (size_t)(slash - p) : strlen(p);

        if (seglen == 0 || (seglen == 1 && p[0] == '.'))
        {
            /* 忽略 */
        }
        else if (seglen == 2 && p[0] == '.' && p[1] == '.')
        {
            while (nlen > 0 && norm[nlen - 1] != '/')
            {
                nlen--;
            }
            if (nlen > 0)
            {
                nlen--;                     /* 去掉末尾'/' */
            }
        }
        else
        {
            if (nlen + 1 + seglen >= sizeof(norm))
            {
                return false;
            }
            norm[nlen++] = '/';
            memcpy(&norm[nlen], p, seglen);
            nlen += seglen;
        }

        if (slash == NULL)
        {
            break;
        }
        p = slash + 1;
    }

    if (nlen == 0)
    {
        norm[nlen++] = '/';                 /* 根目录 */
    }
    norm[nlen] = '\0';

    if (strlen(norm) >= cap)
    {
        return false;
    }

    memcpy(out, norm, nlen + 1);
    return true;
}

/**********************
 *   命令处理函数
 **********************/

static int cmd_help(int argc, char **argv)
{
    struct command **cmds = get_cmds();

    /* help <command>: 显示单条命令的详细说明 */
    if (argc > 1)
    {
        struct command *cmd = find_cmd(argv[1]);

        if (cmd == NULL)
        {
            shell_core_printf("unknown command: %s\r\n", argv[1]);
            return 0;
        }

        shell_core_printf("%s - %s\r\n", cmd->name, cmd->short_help);
        if (cmd->long_help != NULL && cmd->long_help[0] != 0)
        {
            shell_core_printf("  %s\r\n", cmd->long_help);
        }
        return 0;
    }

    /* help: 列出所有命令 */
    shell_core_puts("Commands:\r\n");
    for (int i = 0; cmds[i] != NULL; i++)
    {
        shell_core_printf("  %-8s %s\r\n", cmds[i]->name, cmds[i]->short_help);
    }
    return 0;
}

static int cmd_hello(int argc, char **argv)
{
    shell_core_printf("hello from ESP32-P4 app_shell! argc=%d\r\n", argc);
    for (int i = 0; i < argc; i++)
    {
        shell_core_printf("  argv[%d] = %s\r\n", i, argv[i]);
    }
    return 0;
}

static int cmd_free(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    shell_core_printf("free heap: %u bytes\r\n", (unsigned)esp_get_free_heap_size());
    shell_core_printf("  internal: %u bytes, psram: %u bytes\r\n",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

/* cd [dir]: 改变shell当前目录; 不带参数打印当前目录 */
static int cmd_cd(int argc, char **argv)
{
    char full[CMD_PATH_MAX];
    char cwd[CMD_PATH_MAX];
    DIR *d;

    if (argc < 2)
    {
        shell_core_get_cwd(cwd, sizeof(cwd));
        shell_core_printf("%s\r\n", cwd);
        return 0;
    }

    if (!path_resolve(argv[1], full, sizeof(full)))
    {
        shell_core_printf("cd: invalid path: %s\r\n", argv[1]);
        return -1;
    }

    d = opendir(full);
    if (d == NULL)
    {
        shell_core_printf("cd: %s: no such directory\r\n", full);
        return -1;
    }
    closedir(d);

    shell_core_set_cwd(full);
    shell_core_printf("cwd: %s\r\n", full);
    return 0;
}

/* ls [dir]: 列出目录内容; 不带参数列出当前目录 */
static int cmd_ls(int argc, char **argv)
{
    char full[CMD_PATH_MAX];
    char cwd[CMD_PATH_MAX];
    char child[CMD_CHILD_MAX];
    const char *target;
    struct dirent *e;
    struct stat st;
    DIR *d;
    int count = 0;

    if (argc > 1)
    {
        target = argv[1];
    }
    else
    {
        shell_core_get_cwd(cwd, sizeof(cwd));
        target = cwd;
    }

    if (!path_resolve(target, full, sizeof(full)))
    {
        shell_core_printf("ls: invalid path: %s\r\n", target);
        return -1;
    }

    d = opendir(full);
    if (d == NULL)
    {
        shell_core_printf("ls: %s: cannot open directory\r\n", full);
        return -1;
    }

    shell_core_printf("%s:\r\n", full);

    while ((e = readdir(d)) != NULL)
    {
        bool have_st = false;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
        {
            continue;
        }

        snprintf(child, sizeof(child), "%s/%s", full, e->d_name);
        have_st = (stat(child, &st) == 0);

        if (have_st && S_ISDIR(st.st_mode))
        {
            shell_core_printf("  %s/\r\n", e->d_name);
        }
        else if (have_st)
        {
            shell_core_printf("  %-32s %lld bytes\r\n", e->d_name, (long long)st.st_size);
        }
        else
        {
            shell_core_printf("  %s\r\n", e->d_name);
        }
        count++;
    }

    closedir(d);
    shell_core_printf("%d entries\r\n", count);
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    shell_core_clear();
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    shell_core_puts("rebooting...\r\n");
    LCD_BL(0);                      /* 先关背光, 避免复位期间面板显示默认色 */
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
    return 0;
}

/**********************
 *    命令实例
 **********************/

static struct command help_cmd =
{
    .name = "help",
    .short_help = "list commands",
    .long_help = "list all registered commands",
    .function = cmd_help,
};

static struct command hello_cmd =
{
    .name = "hello",
    .short_help = "print hello and arguments",
    .long_help = "print hello and echo all arguments",
    .function = cmd_hello,
};

static struct command free_cmd =
{
    .name = "free",
    .short_help = "show free heap",
    .long_help = "show total/internal/psram free heap",
    .function = cmd_free,
};

static struct command cd_cmd =
{
    .name = "cd",
    .short_help = "change directory",
    .long_help = "cd [dir]: D: = /User (SD card), C: = /Factory (flash); no arg prints cwd",
    .function = cmd_cd,
};

static struct command ls_cmd =
{
    .name = "ls",
    .short_help = "list directory",
    .long_help = "ls [dir]: list files and directories, default is current directory",
    .function = cmd_ls,
};

static struct command clear_cmd =
{
    .name = "clear",
    .short_help = "clear the LCD console",
    .long_help = "clear both the UART terminal and the LCD console",
    .function = cmd_clear,
};

static struct command reboot_cmd =
{
    .name = "reboot",
    .short_help = "restart the chip",
    .long_help = "restart the chip",
    .function = cmd_reboot,
};

/**********************
 *    命令表
 **********************/

static struct command *g_commands[] =
{
    &help_cmd,
    &hello_cmd,
    &free_cmd,
    &ls_cmd,
    &cd_cmd,
    &clear_cmd,
    &reboot_cmd,
    NULL
};

struct command **get_cmds(void)
{
    return g_commands;
}

struct command *find_cmd(const char *name)
{
    int i = 0;

    while (g_commands[i] != NULL)
    {
        if (strcmp(g_commands[i]->name, name) == 0)
        {
            return g_commands[i];
        }
        i++;
    }
    return NULL;
}
