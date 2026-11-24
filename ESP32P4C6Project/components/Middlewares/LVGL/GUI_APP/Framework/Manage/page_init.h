#ifndef __PAGE_INIT_H__
#define __PAGE_INIT_H__

/**
 * @brief       按固定顺序注册所有 GUI 页面
 * @note        页面注册只是填充 page_manager 的静态表, 不创建 LVGL 控件,
 *              所以在 app_main 里、LVGL 任务启动前调用即可;
 *              wifi_page_init() 内部会执行 esp_netif_init(), 后续 SD/FAT
 *              挂载依赖网络协议栈已初始化, 因此本函数必须先于硬件初始化
 * @param       无
 * @retval      无
 */
void pages_init_all(void);

#endif /* __PAGE_INIT_H__ */
