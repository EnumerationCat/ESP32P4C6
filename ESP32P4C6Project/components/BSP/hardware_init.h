#ifndef __HARDWARE_INIT_H__
#define __HARDWARE_INIT_H__

/**
 * @brief       按固定顺序初始化板级硬件
 * @note        必须在页面模块初始化之后调用: SD/FAT 挂载依赖 WiFi 模块里
 *              已经执行过的 esp_netif_init; 单项失败只告警, 不中断后续初始化
 * @param       无
 * @retval      无
 */
void hardware_init_all(void);

#endif /* __HARDWARE_INIT_H__ */
