#include <stddef.h>

#include "page_init.h"

#include "shell_page.h"
#include "user_home_page.h"
#include "video_page.h"
#include "usb_disk_page.h"
#include "recorder_page.h"
#include "recorder_play_page.h"
#include "camera_page.h"
#include "wifi_page.h"

#include "recorder_event_service.h"
#include "wifi_event_service.h"
#include "video_event_service.h"

/* 顺序沿用原 app_main: shell 最先(其它页面的串口/LCD 输出依赖它),
 * wifi 最后(wifi_page_init 里执行 esp_netif_init, 后续 SD/FAT 挂载依赖它) */
static void (*const k_page_inits[])(void) =
{
    shell_page_init,
    user_home_page_init,
    video_page_init,
    usb_disk_page_init,
    recorder_page_init,
    recorder_play_page_init,
    camera_page_init,
    wifi_page_init,
};

void pages_init_all(void)
{
    for (size_t i = 0; i < sizeof(k_page_inits) / sizeof(k_page_inits[0]); i++)
    {
        k_page_inits[i]();
    }

    /* 服务层: 把后端完成/状态通知桥接成 app_event, 页面只订阅不注册回调.
     * wifi_event_service_init 内部执行 esp_netif_init, 必须在 SD/FAT 挂载前. */
    recorder_event_service_init();
    video_event_service_init();
    wifi_event_service_init();
}
