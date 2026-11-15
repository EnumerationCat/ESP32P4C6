/**
 * @file lv_port_indev.c
 * @brief LVGL input port: GT9xxx capacitive touch panel + USB HID mouse
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "lvgl.h"
#include "touch.h"
#include "usb_hid_mouse.h"

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
static void mouse_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
static void mouse_cursor_create(void);

/**********************
 *   STATIC VARIABLES
 **********************/
static lv_obj_t *s_mouse_cursor = NULL;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;
    static lv_indev_drv_t mouse_drv;
    lv_indev_t *mouse_indev;

    /*
     * The touch controller is already initialized in app_main()
     * (tp_dev.init()), so it is intentionally not initialized here.
     */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);

    /* USB鼠标作为第二个指针设备, 驱动已在app_main中初始化 */
    lv_indev_drv_init(&mouse_drv);
    mouse_drv.type = LV_INDEV_TYPE_POINTER;
    mouse_drv.read_cb = mouse_read;
    mouse_indev = lv_indev_drv_register(&mouse_drv);

    if (mouse_indev != NULL)
    {
        mouse_cursor_create();
        if (s_mouse_cursor != NULL)
        {
            lv_indev_set_cursor(mouse_indev, s_mouse_cursor);
        }
    }
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

/* 创建鼠标光标: 实心圆点(热点在圆点左上角)
 * 注意不能让子对象超出父对象: LVGL按父对象自身区域重绘, 超出部分会残留,
 * 看起来就是"半环"或拖影。 */
static void mouse_cursor_create(void)
{
    if (s_mouse_cursor != NULL)
    {
        return;
    }

    s_mouse_cursor = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_mouse_cursor);
    lv_obj_set_size(s_mouse_cursor, 10, 10);
    lv_obj_clear_flag(s_mouse_cursor, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mouse_cursor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(s_mouse_cursor, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_mouse_cursor, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_mouse_cursor, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_mouse_cursor, 2, 0);
    lv_obj_set_style_border_color(s_mouse_cursor, lv_color_black(), 0);
}

/* USB鼠标指针读取: 把HID增量位移累加成屏幕绝对坐标 */
static void mouse_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static lv_coord_t x = 0;
    static lv_coord_t y = 0;
    static uint8_t buttons = 0;
    static bool pos_inited = false;
    static bool cursor_shown = false;

    usb_hid_mouse_report_t report;
    lv_coord_t hor = lv_disp_get_hor_res(indev_drv->disp);
    lv_coord_t ver = lv_disp_get_ver_res(indev_drv->disp);

    if (!pos_inited)
    {
        x = hor / 2;
        y = ver / 2;
        pos_inited = true;
    }

    if (usb_hid_mouse_read(&report))
    {
        x += (lv_coord_t)report.x;
        y += (lv_coord_t)report.y;
        buttons = report.buttons;

        if (x < 0)
        {
            x = 0;
        }
        else if (x >= hor)
        {
            x = hor - 1;
        }

        if (y < 0)
        {
            y = 0;
        }
        else if (y >= ver)
        {
            y = ver - 1;
        }
    }

    /* 只有插着鼠标时才显示光标 */
    bool connected = usb_hid_mouse_connected();
    if (s_mouse_cursor != NULL && connected != cursor_shown)
    {
        cursor_shown = connected;
        if (connected)
        {
            lv_obj_clear_flag(s_mouse_cursor, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_mouse_cursor, LV_OBJ_FLAG_HIDDEN);
        }
    }

    data->point.x = x;
    data->point.y = y;
    data->state = (buttons & 0x01) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
