/**
 * @file recorder.c
 * @brief WAV 录音机实现(只录音)
 *
 * 参考例程 25_recoding/main/APP/AUDIO/recorder.c, 改为:
 *   - SD 卡走标准 POSIX VFS(fopen/fwrite/mkdir), 挂载点为 /User;
 *   - I2S 复用本工程的 i2s_audio(16kHz 立体声 16bit);
 *   - 录音前强制重建 I2S 通道为 16kHz(视频播放结束后通道可能停留在别的采样率);
 *   - 录音时关闭功放(PA)避免喇叭底噪, 停止后恢复, 不影响后续视频播放。
 */

#include "recorder.h"
#include "i2s_audio.h"
#include "es8311.h"
#include "sdmmc.h"
#include "videoplay.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RECORDER_DIR            MOUNT_POINT "/RECORDER"     /* "/User/RECORDER" */
#define RECORDER_SAMPLE_RATE    16000                       /* 采样率 */
#define RECORDER_CHANNELS       2                           /* 声道数(与I2S立体声一致) */
#define RECORDER_BITS           16                          /* 位宽 */
#define RECORDER_BLOCK_ALIGN    (RECORDER_CHANNELS * RECORDER_BITS / 8)   /* 每帧4字节 */
#define RECORDER_BYTE_RATE      (RECORDER_SAMPLE_RATE * RECORDER_BLOCK_ALIGN) /* 64000 B/s */
#define RECORDER_CHUNK_BYTES    4096                        /* 每次读取约64ms数据 */
#define RECORDER_TASK_STACK     6144                        /* newlib文件操作栈需求较大 */
#define RECORDER_TASK_CORE      1                           /* 与LVGL任务错开 */

static const char *TAG = "recorder";

/* WAV 头(44字节, 线性PCM) */
typedef struct __attribute__((packed))
{
    uint32_t riff_id;           /* "RIFF" */
    uint32_t riff_size;         /* 文件总大小 - 8 */
    uint32_t wave_id;           /* "WAVE" */
    uint32_t fmt_id;            /* "fmt " */
    uint32_t fmt_size;          /* 16 */
    uint16_t audio_format;      /* 1 = PCM */
    uint16_t num_channels;      /* 声道数 */
    uint32_t sample_rate;       /* 采样率 */
    uint32_t byte_rate;         /* 字节速率 */
    uint16_t block_align;       /* 块对齐 */
    uint16_t bits_per_sample;   /* 位宽 */
    uint32_t data_id;           /* "data" */
    uint32_t data_size;         /* PCM 数据大小 */
} wav_header_t;

_Static_assert(sizeof(wav_header_t) == 44, "WAV header must be 44 bytes");

static volatile bool s_recording = false;   /* 是否正在录音 */
static volatile bool s_stop_req = false;    /* 录音任务是否退出 */
static volatile uint32_t s_bytes = 0;       /* 已写入的PCM字节数 */
static SemaphoreHandle_t s_done = NULL;     /* 录音任务收尾完成信号 */
static char s_name[32] = "";               /* 当前/最近一次录音文件名 */

static volatile bool s_playing = false;     /* 是否正在播放 */
static volatile bool s_play_stop_req = false;   /* 播放任务是否退出 */
static SemaphoreHandle_t s_play_done = NULL;    /* 播放任务退出信号 */

/* 播放任务参数(由 recorder_play 申请, 播放任务释放) */
typedef struct
{
    FILE *file;             /* 已定位到PCM数据起始处的文件 */
    uint32_t data_size;     /* PCM数据大小(字节) */
    uint32_t samplerate;    /* 采样率 */
} play_arg_t;

/**
 * @brief       初始化WAV头
 * @param       hdr      : 头结构体指针
 * @param       data_size: PCM数据大小(字节)
 * @retval      无
 */
static void recorder_wav_header_init(wav_header_t *hdr, uint32_t data_size)
{
    memset(hdr, 0, sizeof(*hdr));

    hdr->riff_id         = 0x46464952;                  /* "RIFF" */
    hdr->riff_size       = data_size + 36;              /* 文件总大小 - 8 */
    hdr->wave_id         = 0x45564157;                  /* "WAVE" */
    hdr->fmt_id          = 0x20746D66;                  /* "fmt " */
    hdr->fmt_size        = 16;
    hdr->audio_format    = 1;                           /* PCM */
    hdr->num_channels    = RECORDER_CHANNELS;
    hdr->sample_rate     = RECORDER_SAMPLE_RATE;
    hdr->byte_rate       = RECORDER_BYTE_RATE;
    hdr->block_align     = RECORDER_BLOCK_ALIGN;
    hdr->bits_per_sample = RECORDER_BITS;
    hdr->data_id         = 0x61746164;                  /* "data" */
    hdr->data_size       = data_size;
}

/**
 * @brief       选取第一个未使用的文件名
 * @param       name     : 输出文件名(不含路径)
 * @param       name_size: name 缓冲区大小
 * @param       path     : 输出完整路径
 * @param       path_size: path 缓冲区大小
 * @retval      true:成功; false:编号耗尽
 */
static bool recorder_pick_name(char *name, size_t name_size, char *path, size_t path_size)
{
    uint32_t index;

    for (index = 0; index < 0xFFFF; index++)
    {
        FILE *f;

        snprintf(name, name_size, "REC_%05u.wav", (unsigned)index);
        snprintf(path, path_size, "%s/%s", RECORDER_DIR, name);

        f = fopen(path, "rb");
        if (f == NULL)
        {
            return true;                /* 文件不存在, 该名字可用 */
        }

        fclose(f);
    }

    return false;
}

/**
 * @brief       录音任务: 从I2S读取数据写入WAV文件
 * @param       arg: FILE* 录音文件
 * @retval      无
 */
static void recorder_task(void *arg)
{
    FILE *f = (FILE *)arg;
    uint8_t *buf = malloc(RECORDER_CHUNK_BYTES);
    uint32_t total = 0;

    if (buf == NULL)
    {
        ESP_LOGE(TAG, "no memory for record buffer");
        goto done;
    }

    while (!s_stop_req)
    {
        size_t n = i2s_rx_read(buf, RECORDER_CHUNK_BYTES);

        n -= n % RECORDER_BLOCK_ALIGN;      /* 截断到整帧, 保证WAV合法 */

        if (n == 0)
        {
            continue;
        }

        if (fwrite(buf, 1, n, f) != n)
        {
            ESP_LOGE(TAG, "write failed");
            break;
        }

        total += n;
        s_bytes = total;
    }

done:
    free(buf);

    if (f != NULL)
    {
        wav_header_t hdr;

        recorder_wav_header_init(&hdr, total);
        fseek(f, 0, SEEK_SET);              /* 回到文件头, 回填数据大小 */
        fwrite(&hdr, 1, sizeof(hdr), f);
        fflush(f);
        fclose(f);
    }

    i2s_trx_stop();
    gpio_set_level(PA_CTRL_GPIO_PIN, 1);    /* 恢复功放, 后续视频播放需要 */

    s_bytes = total;
    s_recording = false;
    xSemaphoreGive(s_done);

    ESP_LOGI(TAG, "recording saved: %lu bytes", (unsigned long)total);
    vTaskDelete(NULL);
}

/**
 * @brief       播放任务: 从WAV文件读取PCM并写入I2S
 * @param       arg: play_arg_t*(由任务释放)
 * @retval      无
 */
static void playback_task(void *arg)
{
    play_arg_t *a = (play_arg_t *)arg;
    FILE *f = a->file;
    uint32_t remaining = a->data_size;
    uint32_t samplerate = a->samplerate;
    uint8_t *buf = NULL;
    esp_err_t ret;

    free(a);                                /* 参数已取出, 先释放 */

    ret = i2s_audio_setup_for_samplerate(samplerate, RECORDER_CHUNK_BYTES);
    if (ret == ESP_OK)
    {
        ret = i2s_trx_start();
    }

    if (ret == ESP_OK)
    {
        gpio_set_level(PA_CTRL_GPIO_PIN, 1);    /* 打开功放 */
        buf = malloc(RECORDER_CHUNK_BYTES);
    }

    if (ret != ESP_OK || buf == NULL)
    {
        ESP_LOGE(TAG, "playback start failed");
        free(buf);
        fclose(f);

        if (ret == ESP_OK)
        {
            i2s_trx_stop();
        }

        s_playing = false;
        xSemaphoreGive(s_play_done);
        vTaskDelete(NULL);
        return;
    }

    while (!s_play_stop_req && remaining > 0)
    {
        size_t want = (remaining < RECORDER_CHUNK_BYTES) ? remaining : RECORDER_CHUNK_BYTES;
        size_t nread = fread(buf, 1, want, f);
        size_t nsend;

        if (nread == 0)
        {
            break;
        }

        remaining -= (uint32_t)nread;

        nsend = nread - (nread % RECORDER_BLOCK_ALIGN);     /* 只发送整帧 */
        if (nsend > 0)
        {
            i2s_tx_write(buf, (uint32_t)nsend);
        }
    }

    free(buf);
    fclose(f);
    i2s_trx_stop();

    s_playing = false;
    xSemaphoreGive(s_play_done);

    ESP_LOGI(TAG, "playback finished");
    vTaskDelete(NULL);
}

esp_err_t recorder_start(void)
{
    char path[300];
    char name[32];
    FILE *f;
    wav_header_t hdr;
    esp_err_t ret;

    if (s_recording || s_playing)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (sdmmc_mount_state != SDMMC_MOUNT_STATE_MOUNTED)
    {
        ESP_LOGW(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (videoplay_active())
    {
        ESP_LOGW(TAG, "video is playing");
        return ESP_ERR_INVALID_STATE;
    }

    if (!es8311_ready())
    {
        ESP_LOGW(TAG, "audio codec not ready");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_done == NULL)
    {
        s_done = xSemaphoreCreateBinary();
        if (s_done == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    (void)xSemaphoreTake(s_done, 0);        /* 清掉可能残留的完成信号 */

    mkdir(RECORDER_DIR, 0777);              /* 已存在时返回-1, 忽略 */

    if (!recorder_pick_name(name, sizeof(name), path, sizeof(path)))
    {
        ESP_LOGE(TAG, "no free file name");
        return ESP_FAIL;
    }

    f = fopen(path, "wb");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "open %s failed", path);
        return ESP_FAIL;
    }

    recorder_wav_header_init(&hdr, 0);      /* 先写占位头, 停止时回填 */
    if (fwrite(&hdr, 1, sizeof(hdr), f) != sizeof(hdr))
    {
        ESP_LOGE(TAG, "write header failed");
        fclose(f);
        remove(path);
        return ESP_FAIL;
    }
    fflush(f);

    /* 视频播放可能把I2S改成了别的采样率, 录音前强制重建为16kHz */
    ret = i2s_audio_setup_for_samplerate(RECORDER_SAMPLE_RATE, RECORDER_CHUNK_BYTES);
    if (ret == ESP_OK)
    {
        ret = i2s_trx_start();
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s start failed: %s", esp_err_to_name(ret));
        i2s_trx_stop();             /* 可能只启用了部分通道, 统一停掉 */
        fclose(f);
        remove(path);
        return ret;
    }

    gpio_set_level(PA_CTRL_GPIO_PIN, 0);    /* 录音时关闭功放, 避免喇叭底噪 */

    snprintf(s_name, sizeof(s_name), "%s", name);
    s_bytes = 0;
    s_stop_req = false;
    s_recording = true;

    if (xTaskCreatePinnedToCore(recorder_task, "recorder", RECORDER_TASK_STACK,
                                f, 5, NULL, RECORDER_TASK_CORE) != pdPASS)
    {
        ESP_LOGE(TAG, "create recorder task failed");
        s_recording = false;
        gpio_set_level(PA_CTRL_GPIO_PIN, 1);
        i2s_trx_stop();
        fclose(f);
        remove(path);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "recording -> %s", path);
    return ESP_OK;
}

esp_err_t recorder_stop(void)
{
    if (!s_recording)
    {
        return ESP_OK;
    }

    s_stop_req = true;

    if ((s_done != NULL) &&
        (xSemaphoreTake(s_done, pdMS_TO_TICKS(3000)) != pdTRUE))
    {
        ESP_LOGE(TAG, "stop timeout");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

bool recorder_is_recording(void)
{
    return s_recording;
}

uint32_t recorder_elapsed_sec(void)
{
    return s_bytes / RECORDER_BYTE_RATE;
}

const char *recorder_current_file_name(void)
{
    return s_name;
}

esp_err_t recorder_play(const char *path)
{
    FILE *f;
    wav_header_t hdr;
    play_arg_t *arg;
    uint32_t samplerate;

    if (path == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_recording || s_playing)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!es8311_ready())
    {
        ESP_LOGW(TAG, "audio codec not ready");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_play_done == NULL)
    {
        s_play_done = xSemaphoreCreateBinary();
        if (s_play_done == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    (void)xSemaphoreTake(s_play_done, 0);    /* 清掉可能残留的退出信号 */

    f = fopen(path, "rb");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "open %s failed", path);
        return ESP_FAIL;
    }

    /* 本设备录制的文件固定为44字节头 + PCM数据 */
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        hdr.riff_id != 0x46464952 || hdr.wave_id != 0x45564157 ||
        hdr.data_id != 0x61746164 || hdr.data_size == 0 ||
        hdr.bits_per_sample != RECORDER_BITS || hdr.num_channels != RECORDER_CHANNELS)
    {
        ESP_LOGE(TAG, "not a supported WAV: %s", path);
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }

    samplerate = hdr.sample_rate;

    if (samplerate < 8000 || samplerate > 96000)
    {
        samplerate = RECORDER_SAMPLE_RATE;
    }

    arg = malloc(sizeof(*arg));
    if (arg == NULL)
    {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    arg->file       = f;                    /* 文件指针已位于PCM数据起始处 */
    arg->data_size  = hdr.data_size;
    arg->samplerate = samplerate;

    s_play_stop_req = false;
    s_playing = true;

    if (xTaskCreatePinnedToCore(playback_task, "playback", RECORDER_TASK_STACK,
                                arg, 5, NULL, RECORDER_TASK_CORE) != pdPASS)
    {
        ESP_LOGE(TAG, "create playback task failed");
        s_playing = false;
        free(arg);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "playback -> %s", path);
    return ESP_OK;
}

esp_err_t recorder_play_stop(void)
{
    if (!s_playing)
    {
        return ESP_OK;
    }

    s_play_stop_req = true;

    if ((s_play_done != NULL) &&
        (xSemaphoreTake(s_play_done, pdMS_TO_TICKS(3000)) != pdTRUE))
    {
        ESP_LOGE(TAG, "play stop timeout");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

bool recorder_is_playing(void)
{
    return s_playing;
}
