#include "user_home.h"
#include "lv_btn.h"
#include "lv_disp.h"





lv_obj_t *screen = NULL;




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
    lv_label_set_text(btn_label, "button");
    lv_obj_center(btn_label);
}



