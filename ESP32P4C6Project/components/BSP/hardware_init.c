#include <stddef.h>

#include "hardware_init.h"
#include "esp_err.h"
#include "esp_log.h"

#include "led.h"
#include "lcd.h"
#include "touch.h"
#include "sdmmc.h"
#include "flash_fat.h"
#include "usb_hid_mouse.h"
#include "mipi_cam.h"
#include "i2s_audio.h"
#include "es8311.h"

static const char *TAG = "HW_INIT";

/* 无返回值的 BSP 接口包一层, 统一成 esp_err_t 便于表驱动 */
static esp_err_t hw_led(void)
{
    led_init();
    return ESP_OK;
}

static esp_err_t hw_lcd(void)
{
    lcd_init();
    return ESP_OK;
}

static esp_err_t hw_touch(void)
{
    return tp_dev.init();
}

/* I2S + ES8311: 编解码器复用摄像头 SCCB 的 I2C 总线, 所以必须排在摄像头之后;
 * I2S 起不来就不再初始化 ES8311 */
static esp_err_t hw_audio(void)
{
    esp_err_t err = i2s_audio_init();

    if (err != ESP_OK)
    {
        return err;
    }

    err = es8311_init(mipi_cam_sccb_bus());

    if (err != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(TAG, "audio codec ready");
    return ESP_OK;
}

typedef struct
{
    const char *name;
    esp_err_t (*init)(void);
} hw_init_entry_t;

/* 数组顺序即执行顺序(依赖关系):
 *   - SD/FAT 依赖页面模块里已完成的 esp_netif 初始化, 所以 hardware_init_all()
 *     必须在页面初始化之后调用;
 *   - lcd 必须早于 LVGL 任务里的 lv_port_disp_init();
 *   - audio 依赖 camera 建好的 SCCB I2C 总线, 故排在 camera 之后。 */
static const hw_init_entry_t k_hw_inits[] =
{
    { "sdmmc",     sdmmc_init      },
    { "flash_fat", flash_fat_init  },
    { "led",       hw_led          },
    { "lcd",       hw_lcd          },
    { "touch",     hw_touch        },
    { "usb_mouse", usb_hid_mouse_init },
    { "camera",    mipi_cam_init   },
    { "audio",     hw_audio        },
};

void hardware_init_all(void)
{
    for (size_t i = 0; i < sizeof(k_hw_inits) / sizeof(k_hw_inits[0]); i++)
    {
        const hw_init_entry_t *entry = &k_hw_inits[i];
        esp_err_t err = entry->init();

        if (err != ESP_OK)
        {
            /* 硬件初始化失败只影响对应功能, 不阻塞其它模块 */
            ESP_LOGW(TAG, "%s init failed: %s", entry->name, esp_err_to_name(err));
        }
    }
}
