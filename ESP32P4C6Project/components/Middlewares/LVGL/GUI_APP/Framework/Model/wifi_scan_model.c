/**
 * @file wifi_scan_model.c
 * @brief WiFi扫描数据层实现: 互斥锁保护的SSID缓存 + 换行串拼接
 */

#include "wifi_scan_model.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct
{
    SemaphoreHandle_t lock;                     /* 保护 list/count/options */
    char list[WIFI_MAX_AP][WIFI_SSID_LEN];      /* 扫描到的SSID */
    int count;                                  /* 有效SSID数量 */
    char options[WIFI_OPTIONS_LEN];             /* 下拉框用换行拼接串 */
    volatile bool ready;                        /* 有新结果待取 */
    volatile bool busy;                         /* 扫描进行中 */
} wifi_scan_model_t;

static wifi_scan_model_t s_model = {0};

void wifi_scan_model_init(void)
{
    if (s_model.lock == NULL)
    {
        s_model.lock = xSemaphoreCreateMutex();
    }
}

bool wifi_scan_model_begin(void)
{
    if (s_model.busy)
    {
        return false;
    }

    s_model.busy  = true;
    s_model.ready = false;
    return true;
}

void wifi_scan_model_timeout(void)
{
    s_model.busy = false;
}

bool wifi_scan_model_is_busy(void)
{
    return s_model.busy;
}

bool wifi_scan_model_is_ready(void)
{
    return s_model.ready;
}

void wifi_scan_model_clear_ready(void)
{
    s_model.ready = false;
}

void wifi_scan_model_on_result(int num, wifi_ap_record_t *records)
{
    int n = (num > 0) ? num : 0;
    size_t pos = 0;

    if (n > WIFI_MAX_AP)
    {
        n = WIFI_MAX_AP;
    }

    if (s_model.lock != NULL)
    {
        xSemaphoreTake(s_model.lock, portMAX_DELAY);
    }

    s_model.count = 0;

    for (int i = 0; i < n; i++)
    {
        const char *ssid = (const char *)records[i].ssid;

        if (ssid[0] == '\0')
        {
            continue;               /* 隐藏SSID无法手动连接, 跳过 */
        }

        strlcpy(s_model.list[s_model.count], ssid, WIFI_SSID_LEN);
        s_model.count++;
    }

    /* 同时拼好下拉框用的换行串, 显示层取走即可直接使用 */
    s_model.options[0] = '\0';

    for (int i = 0; i < s_model.count; i++)
    {
        size_t len = strlen(s_model.list[i]);

        if (pos + len + 2 > sizeof(s_model.options))
        {
            break;
        }

        if (pos > 0)
        {
            s_model.options[pos++] = '\n';
        }

        memcpy(&s_model.options[pos], s_model.list[i], len);
        pos += len;
        s_model.options[pos] = '\0';
    }

    if (s_model.lock != NULL)
    {
        xSemaphoreGive(s_model.lock);
    }

    s_model.busy  = false;
    s_model.ready = true;
}

const char *wifi_scan_model_take_options(int *count)
{
    int n;

    if (!s_model.ready)
    {
        return NULL;
    }

    if (s_model.lock != NULL)
    {
        xSemaphoreTake(s_model.lock, portMAX_DELAY);
    }

    n = s_model.count;

    if (s_model.lock != NULL)
    {
        xSemaphoreGive(s_model.lock);
    }

    s_model.ready = false;

    if (count != NULL)
    {
        *count = n;
    }

    return s_model.options;
}
