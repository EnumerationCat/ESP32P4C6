/**
 * @file mjpeg.c
 * @brief MJPEG 帧解码 + 全屏显示
 *
 * 解码输出 RGB565, 按屏幕尺寸做最近邻缩放(保持宽高比, 黑边居中),
 * 直接写入 LCD DPI 双帧缓冲并交给驱动切换, 避免整屏再拷贝一次。
 *
 * 移植自慧勤智远 26_video_player 例程。
 */

#include "mjpeg.h"
#include "lcd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/jpeg_decode.h"

static const char *TAG = "mjpeg";

static jpeg_decoder_handle_t s_jpgd = NULL;       /* JPEG 解码器句柄 */

static jpeg_decode_cfg_t s_decode_cfg = {
    .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
    .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
};

static void *s_fb[2] = {NULL, NULL};              /* LCD 内部帧缓冲(双缓冲) */
static int s_fb_idx = 0;                          /* 下一帧写入的缓冲索引 */
static uint16_t *s_own_buf = NULL;                /* 拿不到帧缓冲时的独立缓冲区 */

static uint32_t s_disp_w = 0;                     /* 目标显示宽 */
static uint32_t s_disp_h = 0;                     /* 目标显示高 */
static int s_disp_x = 0;                          /* 居中偏移 X */
static int s_disp_y = 0;                          /* 居中偏移 Y */

/**
 * @brief       RGB565 最近邻缩放
 * @param       src    : 源缓冲区
 * @param       sstride: 源每行像素数(JPEG 输出按 MCU 对齐, 可能大于显示宽)
 * @param       dw/dh  : 目标宽高
 * @param       dst    : 目标起始地址(已定位到目标区域左上角)
 * @param       dstride: 目标每行像素数
 * @retval      无
 */
static void rgb565_scale_nn(const uint16_t *src, uint32_t sstride, uint32_t sw, uint32_t sh,
                            uint16_t *dst, uint32_t dstride, uint32_t dw, uint32_t dh)
{
    uint32_t xstep = (uint32_t)(((uint64_t)sw << 16) / dw);
    uint32_t ystep = (uint32_t)(((uint64_t)sh << 16) / dh);
    uint32_t yacc = 0;

    for (uint32_t dy = 0; dy < dh; dy++)
    {
        const uint16_t *srow = src + (uint32_t)(yacc >> 16) * sstride;
        uint16_t *drow = dst + (size_t)dy * dstride;
        uint32_t xacc = 0;

        for (uint32_t dx = 0; dx < dw; dx++)
        {
            drow[dx] = srow[xacc >> 16];
            xacc += xstep;
        }

        yacc += ystep;
    }
}

/**
 * @brief       初始化 JPEG 解码器并计算显示参数
 * @param       width/height : 视频源尺寸
 * @param       malloc_size  : 输出缓冲区实际分配大小
 * @retval      解码缓冲区; 失败返回 NULL
 */
uint8_t *mjpegdec_init(uint32_t width, uint32_t height, uint32_t *malloc_size)
{
    jpeg_decode_engine_cfg_t eng_cfg = {
        .intr_priority = 0,
        .timeout_ms    = -1,
    };
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    uint32_t screen_w = lcddev.width;
    uint32_t screen_h = lcddev.height;

    if (jpeg_new_decoder_engine(&eng_cfg, &s_jpgd) != ESP_OK)
    {
        ESP_LOGE(TAG, "create jpeg decoder failed");
        return NULL;
    }

    /* 按宽高比缩放到全屏(黑边居中) */
    s_disp_w = screen_w;
    s_disp_h = (uint32_t)((uint64_t)height * screen_w / width);
    if (s_disp_h > screen_h)
    {
        s_disp_h = screen_h;
        s_disp_w = (uint32_t)((uint64_t)width * screen_h / height);
    }
    s_disp_x = (int)(screen_w - s_disp_w) / 2;
    s_disp_y = (int)(screen_h - s_disp_h) / 2;

    /* LCD 双帧缓冲: 每帧轮换写入, 黑边在打开视频时清黑 */
    s_fb_idx = 0;
    if (esp_lcd_dpi_panel_get_frame_buffer(lcddev.lcd_panel_handle, 2, &s_fb[0], &s_fb[1]) == ESP_OK)
    {
        for (int i = 0; i < 2; i++)
        {
            memset(s_fb[i], 0, (size_t)screen_w * screen_h * sizeof(uint16_t));
            esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, 0, 0, screen_w, screen_h, s_fb[i]);
        }
    }
    else
    {
        /* 退路: 自备缓冲区, 由驱动拷贝到内部帧缓冲 */
        s_fb[0] = NULL;
        s_fb[1] = NULL;
        s_own_buf = heap_caps_malloc((size_t)screen_w * screen_h * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (s_own_buf == NULL)
        {
            ESP_LOGE(TAG, "no memory for video buffer");
            jpeg_del_decoder_engine(s_jpgd);
            s_jpgd = NULL;
            return NULL;
        }
        memset(s_own_buf, 0, (size_t)screen_w * screen_h * sizeof(uint16_t));
    }

    /* JPEG 输出按 MCU 对齐(最大 16 像素), 分配时再留 32 像素余量 */
    uint32_t buf_w = (width + 31) & ~31u;
    uint32_t buf_h = (height + 31) & ~31u;

    ESP_LOGI(TAG, "video %lux%lu -> %lux%lu @(%d,%d)",
             (unsigned long)width, (unsigned long)height,
             (unsigned long)s_disp_w, (unsigned long)s_disp_h, s_disp_x, s_disp_y);

    uint8_t *out = (uint8_t *)jpeg_alloc_decoder_mem(buf_w * buf_h * 2, &mem_cfg, (size_t *)malloc_size);
    if (out == NULL)
    {
        ESP_LOGE(TAG, "no memory for decoder output");
        mjpegdec_free(NULL);
    }

    return out;
}

/**
 * @brief       解码一帧 JPEG 并显示
 * @param       inbuf/inbuf_size: JPEG 数据
 * @param       outbuf/outbuf_size: 解码缓冲区
 * @param       width/height: 视频源尺寸
 * @retval      0:成功; 1:失败
 */
uint8_t mjpegdec_decode(uint8_t *inbuf, uint32_t inbuf_size, uint8_t *outbuf,
                        uint32_t outbuf_size, uint32_t width, uint32_t height)
{
    static uint32_t out_size = 0;
    uint16_t *target;
    uint32_t sstride = (width + 15) & ~15u;    /* JPEG 输出行按 MCU 对齐 */
    uint32_t dstride = lcddev.width;

    if (s_jpgd == NULL || inbuf_size == 0)
    {
        return 1;
    }

    if (jpeg_decoder_process(s_jpgd, &s_decode_cfg, inbuf, inbuf_size,
                             outbuf, outbuf_size, &out_size) != ESP_OK || out_size == 0)
    {
        return 1;
    }

    if (s_fb[0] != NULL)
    {
        target = (uint16_t *)s_fb[s_fb_idx];
        s_fb_idx ^= 1;
    }
    else
    {
        target = s_own_buf;
        memset(target, 0, (size_t)lcddev.width * lcddev.height * sizeof(uint16_t));  /* 清黑边 */
    }

    rgb565_scale_nn((uint16_t *)outbuf, sstride, width, height,
                    target + (size_t)s_disp_y * dstride + s_disp_x, dstride,
                    s_disp_w, s_disp_h);

    /* 目标是内部帧缓冲时驱动只做 cache 回写并切换; 自备缓冲则整屏拷贝 */
    esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, 0, 0,
                              lcddev.width, lcddev.height, target);

    return 0;
}

/**
 * @brief       卸载 JPEG 解码器并释放缓冲区
 * @param       buf: mjpegdec_init 返回的解码缓冲区
 * @retval      无
 */
void mjpegdec_free(uint8_t *buf)
{
    if (buf != NULL)
    {
        heap_caps_free(buf);
    }

    if (s_own_buf != NULL)
    {
        heap_caps_free(s_own_buf);
        s_own_buf = NULL;
    }

    s_fb[0] = NULL;
    s_fb[1] = NULL;
    s_fb_idx = 0;
    s_disp_w = 0;
    s_disp_h = 0;
    s_disp_x = 0;
    s_disp_y = 0;

    if (s_jpgd != NULL)
    {
        jpeg_del_decoder_engine(s_jpgd);
        s_jpgd = NULL;
    }
}
