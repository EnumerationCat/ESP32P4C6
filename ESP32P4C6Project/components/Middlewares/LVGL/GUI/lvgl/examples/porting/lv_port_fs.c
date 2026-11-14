/**
 * @file lv_port_fs.c
 * @brief LVGL 文件系统适配: 基于 ESP-IDF VFS(FatFs) 的 POSIX 接口
 *
 * 盘符与BSP挂载点对应:
 *   D: -> /User     SD卡        (components/BSP/SDMMC/sdmmc.h: MOUNT_POINT)
 *   C: -> /Factory  Flash FAT   (components/BSP/FLASH_FAT/flash_fat.h: FACTORY_MOUNT_POINT)
 * 例如 "D:/img/bg.bmp" 实际以 "/User/img/bg.bmp" 打开。
 *
 * 挂载由BSP完成, 本文件只向LVGL注册驱动, 因此 lv_port_fs_init() 需在 lv_init() 之后调用。
 * LVGL的路径长度上限为 LV_FS_MAX_PATH_LENGTH(256), 拼接挂载点后留一倍余量。
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_fs.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/*********************
 *      DEFINES
 *********************/
#define FS_SD_LETTER        'D'         /* SD卡盘符 */
#define FS_SD_ROOT          "/User"     /* SD卡挂载点 */
#define FS_FLASH_LETTER     'C'         /* Flash FAT盘符 */
#define FS_FLASH_ROOT       "/Factory"  /* Flash FAT挂载点 */

/* 拼接后的完整VFS路径缓冲(挂载点 + LVGL路径 + 文件名) */
#define FS_PATH_BUF         (LV_FS_MAX_PATH_LENGTH + LV_FS_MAX_FN_LENGTH + 8)

/**********************
 *      TYPEDEFS
 **********************/

/* 目录句柄: 保存DIR*和目录完整路径(dir_read中stat每一项需要) */
typedef struct
{
    DIR *dir;
    char path[FS_PATH_BUF];
} fs_dir_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void fs_drv_setup(lv_fs_drv_t *drv, char letter, const char *root);

static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode);
static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p);
static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br);
static lv_fs_res_t fs_write(lv_fs_drv_t *drv, void *file_p, const void *buf, uint32_t btw, uint32_t *bw);
static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence);
static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p);

static void *fs_dir_open(lv_fs_drv_t *drv, const char *path);
static lv_fs_res_t fs_dir_read(lv_fs_drv_t *drv, void *rddir_p, char *fn);
static lv_fs_res_t fs_dir_close(lv_fs_drv_t *drv, void *rddir_p);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_fs_init(void)
{
    /* 两个静态驱动描述符: lv_fs_drv_register()只保存指针, 生命周期必须覆盖整个运行期 */
    static lv_fs_drv_t sd_drv;
    static lv_fs_drv_t flash_drv;

    fs_drv_setup(&sd_drv, FS_SD_LETTER, FS_SD_ROOT);
    lv_fs_drv_register(&sd_drv);

    fs_drv_setup(&flash_drv, FS_FLASH_LETTER, FS_FLASH_ROOT);
    lv_fs_drv_register(&flash_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/* 用同一套回调注册一个盘符, user_data保存挂载点(如"/User") */
static void fs_drv_setup(lv_fs_drv_t *drv, char letter, const char *root)
{
    lv_fs_drv_init(drv);

    drv->letter     = letter;
    drv->cache_size = 0;                    /* 不做LVGL层缓存, 直接走VFS */
    drv->user_data  = (void *)root;

    drv->open_cb  = fs_open;
    drv->close_cb = fs_close;
    drv->read_cb  = fs_read;
    drv->write_cb = fs_write;
    drv->seek_cb  = fs_seek;
    drv->tell_cb  = fs_tell;

    drv->dir_open_cb  = fs_dir_open;
    drv->dir_read_cb  = fs_dir_read;
    drv->dir_close_cb = fs_dir_close;
}

/* 把LVGL路径(已去掉盘符, 如"/img/bg.bmp")拼成VFS完整路径(如"/User/img/bg.bmp") */
static bool fs_build_path(lv_fs_drv_t *drv, const char *path, char *buf, size_t cap)
{
    int n;

    if (drv == NULL || path == NULL || buf == NULL || cap == 0)
    {
        return false;
    }

    n = snprintf(buf, cap, "%s%s", (const char *)drv->user_data, path);
    return n > 0 && (size_t)n < cap;
}

/* 把fopen模式转换为LVGL打开模式 */
static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    char buf[FS_PATH_BUF];
    const char *fmode;

    if (!fs_build_path(drv, path, buf, sizeof(buf)))
    {
        return NULL;
    }

    if (mode == LV_FS_MODE_WR)
    {
        fmode = "wb";
    }
    else if (mode == LV_FS_MODE_RD)
    {
        fmode = "rb";
    }
    else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD))
    {
        fmode = "r+b";
    }
    else
    {
        return NULL;
    }

    return (void *)fopen(buf, fmode);
}

static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p)
{
    LV_UNUSED(drv);

    if (file_p == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    return fclose((FILE *)file_p) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    size_t n;

    LV_UNUSED(drv);

    if (file_p == NULL || br == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    n = fread(buf, 1, btr, (FILE *)file_p);
    *br = (uint32_t)n;

    /* 读不满且出错才算错误; 到文件尾正常返回 */
    return (n < btr && ferror((FILE *)file_p)) ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

static lv_fs_res_t fs_write(lv_fs_drv_t *drv, void *file_p, const void *buf, uint32_t btw, uint32_t *bw)
{
    size_t n;

    LV_UNUSED(drv);

    if (file_p == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    n = fwrite(buf, 1, btw, (FILE *)file_p);
    if (bw != NULL)
    {
        *bw = (uint32_t)n;
    }

    return n == btw ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    int w;

    LV_UNUSED(drv);

    if (file_p == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    switch (whence)
    {
        case LV_FS_SEEK_SET: w = SEEK_SET; break;
        case LV_FS_SEEK_CUR: w = SEEK_CUR; break;
        case LV_FS_SEEK_END: w = SEEK_END; break;
        default:             return LV_FS_RES_INV_PARAM;
    }

    return fseek((FILE *)file_p, (long)pos, w) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    long pos;

    LV_UNUSED(drv);

    if (file_p == NULL || pos_p == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    pos = ftell((FILE *)file_p);
    if (pos < 0)
    {
        return LV_FS_RES_UNKNOWN;
    }

    *pos_p = (uint32_t)pos;
    return LV_FS_RES_OK;
}

static void *fs_dir_open(lv_fs_drv_t *drv, const char *path)
{
    fs_dir_t *d;

    d = (fs_dir_t *)calloc(1, sizeof(fs_dir_t));
    if (d == NULL)
    {
        return NULL;
    }

    if (!fs_build_path(drv, path, d->path, sizeof(d->path)))
    {
        free(d);
        return NULL;
    }

    d->dir = opendir(d->path);
    if (d->dir == NULL)
    {
        free(d);
        return NULL;
    }

    return d;
}

/* 读下一个文件名; 目录名以'/'开头(VFS的dirent不保证d_type, 用stat判断) */
static lv_fs_res_t fs_dir_read(lv_fs_drv_t *drv, void *rddir_p, char *fn)
{
    fs_dir_t *d = (fs_dir_t *)rddir_p;
    struct dirent *e;
    struct stat st;
    char full[FS_PATH_BUF];

    LV_UNUSED(drv);

    if (d == NULL || d->dir == NULL || fn == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    for (;;)
    {
        e = readdir(d->dir);
        if (e == NULL)
        {
            fn[0] = '\0';                   /* 目录结束 */
            return LV_FS_RES_OK;
        }

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
        {
            continue;
        }

        snprintf(full, sizeof(full), "%s/%s", d->path, e->d_name);
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
        {
            snprintf(fn, LV_FS_MAX_FN_LENGTH, "/%s", e->d_name);
        }
        else
        {
            snprintf(fn, LV_FS_MAX_FN_LENGTH, "%s", e->d_name);
        }

        return LV_FS_RES_OK;
    }
}

static lv_fs_res_t fs_dir_close(lv_fs_drv_t *drv, void *rddir_p)
{
    fs_dir_t *d = (fs_dir_t *)rddir_p;

    LV_UNUSED(drv);

    if (d == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    closedir(d->dir);
    free(d);

    return LV_FS_RES_OK;
}
