#include "user_home.h"
#include "app_shell.h"
#include "lv_btn.h"
#include "lv_disp.h"


lv_obj_t *screen = NULL;

static user_home_camera_cb_t camera_cb = NULL;


/**
 * @brief       注册相机按钮回调
 * @param       cb: 回调函数
 * @retval      无
 */
void user_home_set_camera_cb(user_home_camera_cb_t cb)
{
    camera_cb = cb;
}

/**
 * @brief       相机按钮事件回调
 * @param       e: LVGL事件
 * @retval      无
 */
static void camera_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (camera_cb)
    {
        camera_cb();            /* 进入摄像头全屏预览, 触摸屏幕任意位置退出 */
    }
}

/**
 * @brief       命令行按钮事件回调
 * @param       e: LVGL事件
 * @retval      无
 */
static void shell_btn_event_cb(lv_event_t *e)
{
    (void)e;
    app_shell_open();           /* 切换到LCD文本页, 点HOME返回 */
}




void user_home(void)
{
	//获取当前活跃屏幕
    screen = lv_scr_act();
    // 创建根容器（全屏，方便管理控件）
    lv_obj_t *object = lv_obj_create(screen);
    lv_obj_set_size(object, LV_HOR_RES, LV_VER_RES); // 全屏
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN); // 去掉边框

    // ==================== 相机启动按钮 ====================
    lv_obj_t *button = lv_btn_create(object);
    lv_obj_set_size(button, 60, 60);        // 放大按钮，方便点击
    lv_obj_set_pos(button, 90, 130);       // 调整位置

    // 按钮添加文字（相机图标/文字）
    lv_obj_t *btn_label = lv_label_create(button);
    lv_label_set_text(btn_label, "CAM");
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(button, camera_btn_event_cb, LV_EVENT_CLICKED, NULL);

    // ==================== 命令行页面按钮 ====================
    lv_obj_t *shell_btn = lv_btn_create(object);
    lv_obj_set_size(shell_btn, 60, 60);
    lv_obj_set_pos(shell_btn, 190, 130);

    lv_obj_t *shell_label = lv_label_create(shell_btn);
    lv_label_set_text(shell_label, "SHL");
    lv_obj_center(shell_label);

    lv_obj_add_event_cb(shell_btn, shell_btn_event_cb, LV_EVENT_CLICKED, NULL);
}



