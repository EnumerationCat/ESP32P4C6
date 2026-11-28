/**
 * @file file_list_model.c
 * @brief 通用文件列表数据层实现: dirent 扫描 + 调用方实例里的路径缓存
 */

#include "file_list_model.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "file_list";

/* 判断文件名是否以 ext 结尾(不区分大小写); ext 可带或不带前导'.' */
static bool has_ext(const char *name, const char *ext)
{
    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);

    if (ext_len > 0 && ext[0] == '.')
    {
        ext++;
        ext_len--;
    }

    /* 至少要有 1 个字符的基名 + '.' + ext */
    if (ext_len == 0 || name_len < ext_len + 2)
    {
        return false;
    }

    const char *dot = name + (name_len - ext_len - 1);

    if (*dot != '.')
    {
        return false;
    }

    dot++;

    for (size_t i = 0; i < ext_len; i++)
    {
        if (tolower((unsigned char)dot[i]) != tolower((unsigned char)ext[i]))
        {
            return false;
        }
    }

    return true;
}

void file_list_scan(file_list_t *list, const char *dir, const char *ext)
{
    DIR *d;
    struct dirent *ent;

    if (list == NULL)
    {
        return;
    }

    list->count = 0;

    if (dir == NULL || ext == NULL)
    {
        return;
    }

    d = opendir(dir);
    if (d == NULL)
    {
        ESP_LOGW(TAG, "open %s failed", dir);
        return;
    }

    while ((ent = readdir(d)) != NULL && list->count < FILE_LIST_MAX)
    {
        if (!has_ext(ent->d_name, ext))
        {
            continue;
        }

        snprintf(list->paths[list->count], FILE_LIST_PATH_LEN, "%s/%s", dir, ent->d_name);
        list->count++;
    }

    closedir(d);

    ESP_LOGI(TAG, "%s: found %d file(s)", dir, list->count);
}

int file_list_count(const file_list_t *list)
{
    return (list != NULL) ? list->count : 0;
}

const char *file_list_name(const file_list_t *list, int index)
{
    const char *path = file_list_path(list, index);
    const char *base;

    if (path == NULL)
    {
        return NULL;
    }

    base = strrchr(path, '/');

    return (base != NULL) ? base + 1 : path;
}

const char *file_list_path(const file_list_t *list, int index)
{
    if (list == NULL || index < 0 || index >= list->count)
    {
        return NULL;
    }

    return list->paths[index];
}
