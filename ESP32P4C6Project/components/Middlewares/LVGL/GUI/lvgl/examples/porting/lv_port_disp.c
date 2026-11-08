/**
 * @file lv_port_disp.c
 * @brief LVGL display port for the 3.5" ST7796U MIPI-DSI panel (320x480, RGB565)
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include "lvgl.h"
#include "lcd.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

/*********************
 *      DEFINES
 *********************/
#define MY_DISP_HOR_RES 320
#define MY_DISP_VER_RES 480
#define DRAW_BUF_LINES  40

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);

/**********************
 *  STATIC VARIABLES
 **********************/
/* Two partial draw buffers in internal RAM (2 x 320 x 40 x 2 bytes = 51.2 KB) */
static lv_color_t buf_1[MY_DISP_HOR_RES * DRAW_BUF_LINES];
static lv_color_t buf_2[MY_DISP_HOR_RES * DRAW_BUF_LINES];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_port_disp_init(void)
{
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;

    if (lcddev.lcd_panel_handle == NULL)
    {
        ESP_LOGE("lv_port_disp", "LCD panel is not initialized");
        return;
    }

    lv_disp_draw_buf_init(&draw_buf, buf_1, buf_2, MY_DISP_HOR_RES * DRAW_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = lcddev.width;
    disp_drv.ver_res = lcddev.height;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    /*
     * esp_lcd_panel_draw_bitmap() copies synchronously into the scanned
     * frame buffer and performs the cache write-back before returning
     * (DMA2D hook is not enabled), so the buffer can be released at once.
     * The end coordinates are exclusive.
     */
    esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle,
                              area->x1,
                              area->y1,
                              area->x2 + 1,
                              area->y2 + 1,
                              color_p);

    lv_disp_flush_ready(disp_drv);
}
