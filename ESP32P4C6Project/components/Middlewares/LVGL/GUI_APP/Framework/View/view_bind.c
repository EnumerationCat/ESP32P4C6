/**
 * @file view_bind.c
 * @brief 视图控件绑定辅助实现: LVGL点击 -> manager_click
 */

#include "view_bind.h"
#include "page_manager.h"

/* 点击适配: click_id 放在事件 user_data, user_data 放在控件 user_data */
static void view_click_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    const char *click_id = (const char *)lv_event_get_user_data(e);
    intptr_t user_data = (intptr_t)lv_obj_get_user_data(obj);

    manager_click(click_id, user_data);
}

void view_bind_click(lv_obj_t *obj, const char *click_id, intptr_t user_data)
{
    if (obj == NULL || click_id == NULL)
    {
        return;
    }

    lv_obj_set_user_data(obj, (void *)user_data);
    lv_obj_add_event_cb(obj, view_click_cb, LV_EVENT_CLICKED, (void *)click_id);
}
