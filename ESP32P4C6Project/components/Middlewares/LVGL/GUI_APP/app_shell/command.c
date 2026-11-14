/**
 * @file command.c
 * @brief app_shell 命令实现与命令表(参考 GD32 bootloader 的 command.c)
 */

#include "command.h"
#include "app_shell.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include <stddef.h>
#include <string.h>

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
            app_shell_printf("unknown command: %s\r\n", argv[1]);
            return 0;
        }

        app_shell_printf("%s - %s\r\n", cmd->name, cmd->short_help);
        if (cmd->long_help != NULL && cmd->long_help[0] != 0)
        {
            app_shell_printf("  %s\r\n", cmd->long_help);
        }
        return 0;
    }

    /* help: 列出所有命令 */
    app_shell_puts("Commands:\r\n");
    for (int i = 0; cmds[i] != NULL; i++)
    {
        app_shell_printf("  %-8s %s\r\n", cmds[i]->name, cmds[i]->short_help);
    }
    return 0;
}

static int cmd_hello(int argc, char **argv)
{
    app_shell_printf("hello from ESP32-P4 app_shell! argc=%d\r\n", argc);
    for (int i = 0; i < argc; i++)
    {
        app_shell_printf("  argv[%d] = %s\r\n", i, argv[i]);
    }
    return 0;
}

static int cmd_free(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    app_shell_printf("free heap: %u bytes\r\n", (unsigned)esp_get_free_heap_size());
    app_shell_printf("  internal: %u bytes, psram: %u bytes\r\n",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    app_shell_clear();
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    app_shell_puts("rebooting...\r\n");
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
