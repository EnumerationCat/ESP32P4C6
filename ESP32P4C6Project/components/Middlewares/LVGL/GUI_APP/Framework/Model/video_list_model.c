/**
 * @file video_list_model.c
 * @brief 视频文件列表数据层实现: dirent 扫描 + 静态路径缓存
 */

#include "video_list_model.h"
#include "videoplay.h"          /* VIDEOPLAY_DIR */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "video_list";

static char s_paths[VIDEO_LIST_MAX][VIDEO_LIST_PATH_LEN];
static char s_names[VIDEO_LIST_MAX][VIDEO_LIST_PATH_LEN];
static int s_count = 0;

/* 判断文件名是否以 .avi 结尾(不区分大小写) */
static bool is_avi_name(const char *name)
{
    size_t len = strlen(name);

    if (len < 5)
    {
        return false;
    }

    const char *ext = name + len - 4;

    return ext[0] == '.' &&
           tolower((unsigned char)ext[1]) == 'a' &&
           tolower((unsigned char)ext[2]) == 'v' &&
           tolower((unsigned char)ext[3]) == 'i';
}

void video_list_model_refresh(void)
{
    DIR *dir;
    struct dirent *ent;

    s_count = 0;

    dir = opendir(VIDEOPLAY_DIR);
    if (dir == NULL)
    {
        ESP_LOGW(TAG, "open %s failed", VIDEOPLAY_DIR);
        return;
    }

    while ((ent = readdir(dir)) != NULL && s_count < VIDEO_LIST_MAX)
    {
        if (!is_avi_name(ent->d_name))
        {
            continue;
        }

        snprintf(s_paths[s_count], sizeof(s_paths[0]), "%s/%s", VIDEOPLAY_DIR, ent->d_name);
        snprintf(s_names[s_count], sizeof(s_names[0]), "%s", ent->d_name);
        s_count++;
    }

    closedir(dir);

    ESP_LOGI(TAG, "found %d avi file(s)", s_count);
}

int video_list_model_count(void)
{
    return s_count;
}

const char *video_list_model_name(int index)
{
    if (index < 0 || index >= s_count)
    {
        return NULL;
    }

    return s_names[index];
}

const char *video_list_model_path(int index)
{
    if (index < 0 || index >= s_count)
    {
        return NULL;
    }

    return s_paths[index];
}
