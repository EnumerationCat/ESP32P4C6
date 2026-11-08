/**
 * @file lv_port_indev.c
 * @brief LVGL input port for the GT9xxx capacitive touch panel
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "lvgl.h"
#include "touch.h"

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    /*
     * The touch controller is already initialized in app_main()
     * (tp_dev.init()), so it is intentionally not initialized here.
     */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    (void)indev_drv;

    tp_dev.scan(0);

    if ((tp_dev.sta & TP_PRES_DOWN) &&
        tp_dev.x[0] < lcddev.width && tp_dev.y[0] < lcddev.height)
    {
        last_x = tp_dev.x[0];
        last_y = tp_dev.y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* Keep reporting the last valid coordinates while released */
    data->point.x = last_x;
    data->point.y = last_y;
}
