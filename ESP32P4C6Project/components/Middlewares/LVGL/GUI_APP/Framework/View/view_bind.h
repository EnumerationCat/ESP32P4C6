/**
 * @file view_bind.h
 * @brief 视图控件绑定辅助(Framework/View): 把LVGL点击转成框架的 manager_click
 *
 * 点击链路: 控件 LV_EVENT_CLICKED -> view_bind 的适配回调 -> manager_click()
 *           -> 当前页 on_click(self, click_id, user_data)
 *
 * 与 on_event 的分工:
 *  - on_click: 用户输入(按钮/列表行), 在LVGL任务中同步派发;
 *  - on_event: 异步事件(Service/后端经 app_event 投递)。
 */

#ifndef VIEW_BIND_H
#define VIEW_BIND_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       给控件绑定点击ID(覆盖已有的 user_data)
 * @note        只能在LVGL任务中调用; user_data 存在控件的 user_data 里,
 *              若控件本身需要 user_data 请改用其它方式传参
 * @param       obj     : 控件(通常是 lv_btn)
 * @param       click_id: 点击ID(必须是长生命周期字符串, 建议字面量)
 * @param       user_data: 附加参数(如列表行号), 无则传0
 * @retval      无
 */
void view_bind_click(lv_obj_t *obj, const char *click_id, intptr_t user_data);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_BIND_H */
