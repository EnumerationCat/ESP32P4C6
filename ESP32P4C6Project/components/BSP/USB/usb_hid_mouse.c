/**
 * @file usb_hid_mouse.c
 * @brief USB HID 鼠标驱动
 * 把最新位移/按键保存在加锁的共享状态里, 由LVGL输入设备轮询读取。
 */

#include "usb_hid_mouse.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_mouse.h"

static const char *TAG = "USB_MOUSE";

/* HID协议名称(日志用) */
static const char *hid_proto_name_str[] = {
    "NONE",
    "KEYBOARD",
    "MOUSE"
};

/* 报表回调(USB任务)与读取方(LVGL任务)共享的状态 */
typedef struct
{
    int32_t dx;         /* 累计X位移 */
    int32_t dy;         /* 累计Y位移 */
    uint8_t buttons;    /* 最新按键状态 */
    bool pending;       /* 有未被读取的报表 */
    bool connected;     /* 鼠标在线 */
    bool mouse_iface;   /* 设备中已发现标准鼠标接口(用于非标准鼠标的兼容判断) */
} usb_mouse_state_t;

/*
 * 很多2.4G接收器/复合设备不按boot报文(按键,x,y)上报, 而是带Report ID、
 * 字段顺序不同或有16位轴值(如小米接收器实际是 [ID, 按键, X, Y])。
 * 直接按boot解析会把X当Y(左右移动变成上下移动), 按住按键时按键字节还被
 * 累加进X(光标冲到屏幕边缘)。因此解析HID报告描述符, 按真实比特偏移取字段。
 */
#define HID_MAX_LAYOUTS     4       /* 最多支持的Report ID种类 */
#define HID_MAX_USAGES      16      /* 单个主条目前的Usage数量 */

typedef struct
{
    uint8_t report_id;          /* 0: 描述符未使用Report ID */
    bool has_x;
    bool has_y;
    uint16_t x_bit;             /* X字段比特偏移(不含Report ID字节) */
    uint16_t y_bit;             /* Y字段比特偏移(不含Report ID字节) */
    uint16_t buttons_bit;       /* 第一个按键比特偏移 */
    uint8_t x_size;             /* 字段位宽 */
    uint8_t y_size;
    uint8_t buttons_num;        /* 按键位数 */
    bool x_signed;
    bool y_signed;
} hid_mouse_layout_t;

/* 一套报表布局表: 同一描述符内不同Report ID的字段位置 */
typedef struct
{
    hid_mouse_layout_t items[HID_MAX_LAYOUTS];
    int count;
    bool has_report_id;
} hid_layout_table_t;

/* 布局数据库: 解析时先写到parse, 只有找到X/Y才整体提交到live,
 * 避免键盘/厂商接口的描述符把已解析好的鼠标布局清掉 */
typedef struct
{
    hid_layout_table_t live;        /* 当前生效的布局 */
    hid_layout_table_t parse;       /* 解析临时区 */
    portMUX_TYPE lock;              /* 保护live区 */
} hid_layout_db_t;

/* 驱动全部模块级状态: USB回调、LVGL读取、布局任务共享 */
typedef struct
{
    usb_mouse_state_t state;            /* 累计位移/按键/连接状态 */
    portMUX_TYPE state_lock;            /* 保护state */

    hid_layout_db_t layouts;            /* 报告描述符解析出的布局 */

    QueueHandle_t layout_queue;         /* 接口启动任务队列 */
    TaskHandle_t layout_task;           /* 取描述符/启动接口的任务 */
    SemaphoreHandle_t layout_stopped;   /* layout_task退出完成 */
    SemaphoreHandle_t host_uninstalled; /* usb_host_uninstall完成 */
    int debug_reports;                  /* 已打印的调试报文数 */
    bool installed;                     /* USB Host/HID当前已安装 */
} usb_hid_mouse_ctx_t;

static usb_hid_mouse_ctx_t s_ctx = {
    .state_lock = portMUX_INITIALIZER_UNLOCKED,
    .layouts = {
        .lock = portMUX_INITIALIZER_UNLOCKED,
    },
};

static hid_mouse_layout_t *hid_parse_layout_get(uint8_t report_id)
{
    hid_layout_table_t *table = &s_ctx.layouts.parse;

    for (int i = 0; i < table->count; i++)
    {
        if (table->items[i].report_id == report_id)
        {
            return &table->items[i];
        }
    }

    if (table->count >= HID_MAX_LAYOUTS)
    {
        return NULL;
    }

    hid_mouse_layout_t *layout = &table->items[table->count++];
    memset(layout, 0, sizeof(*layout));
    layout->report_id = report_id;
    return layout;
}

/* 描述符解析过程中的Global/Local条目状态 */
typedef struct
{
    uint32_t usage_page;            /* Global: Usage Page */
    uint32_t report_size;           /* Global: Report Size */
    uint32_t report_count;          /* Global: Report Count */
    int32_t logical_min;            /* Global: Logical Minimum */
    uint32_t bit_offset;            /* 当前报表内的比特偏移 */

    uint32_t usages[HID_MAX_USAGES];        /* Local: 当前主条目之前声明的Usage */
    uint32_t usage_pages[HID_MAX_USAGES];   /* 0: 沿用当前Usage Page */
    size_t usage_count;
    uint32_t usage_min;
    uint32_t usage_max;
    bool has_usage_range;

    /* Push/Pop保存的Global状态 */
    struct
    {
        uint32_t page;
        uint32_t size;
        uint32_t count;
        int32_t min;
    } stack[4];
    int stack_depth;
} hid_desc_ctx_t;

/**
 * @brief       处理Input主条目, 按当前Global/Local状态记录字段位置
 * @param       ctx:解析上下文
 * @param       layout:当前Report ID对应的布局
 * @param       flags:Input主条目数据
 * @retval      无
 */
static void hid_desc_handle_input(hid_desc_ctx_t *ctx, hid_mouse_layout_t *layout, uint32_t flags)
{
    /* HID规范: bit0=0表示Data, bit1=1表示Variable
     * (0x02=Data,Var,Abs按键; 0x06=Data,Var,Rel轴; 0x03=常量填充) */
    bool is_data = (flags & 0x01) == 0;
    bool is_var = (flags & 0x02) != 0;

    for (uint32_t n = 0; n < ctx->report_count; n++)
    {
        uint32_t usage = 0;
        uint32_t page = ctx->usage_page;
        bool have_usage = false;

        if (n < ctx->usage_count)
        {
            usage = ctx->usages[n];
            if (ctx->usage_pages[n] != 0)
            {
                page = ctx->usage_pages[n];
            }
            have_usage = true;
        }
        else if (ctx->has_usage_range && (ctx->usage_min + n) <= ctx->usage_max)
        {
            usage = ctx->usage_min + n;
            have_usage = true;
        }

        if (is_data && is_var && have_usage)
        {
            /* 有的描述符在X/Y之前忘了切回Generic Desktop页,
             * 这里除标准页外, 只要不是按键/消费类页面且位宽>=8也认作轴 */
            bool axis_page = (page == 0x01) ||
                             (page != 0x09 && page != 0x0C && ctx->report_size >= 8);

            if (axis_page && usage == 0x30)           /* Generic Desktop X */
            {
                layout->x_bit = (uint16_t)ctx->bit_offset;
                layout->x_size = (uint8_t)ctx->report_size;
                layout->x_signed = (ctx->logical_min < 0);
                layout->has_x = true;
            }
            else if (axis_page && usage == 0x31)      /* Generic Desktop Y */
            {
                layout->y_bit = (uint16_t)ctx->bit_offset;
                layout->y_size = (uint8_t)ctx->report_size;
                layout->y_signed = (ctx->logical_min < 0);
                layout->has_y = true;
            }
            else if ((page == 0x09 || page == 0) && usage >= 1 && usage <= 8)   /* Button */
            {
                if (layout->buttons_num == 0)
                {
                    layout->buttons_bit = (uint16_t)ctx->bit_offset;
                }
                layout->buttons_num++;
            }
        }

        ctx->bit_offset += ctx->report_size;
    }

    ctx->usage_count = 0;
    ctx->has_usage_range = false;
}

/**
 * @brief       解析HID报告描述符, 提取X/Y/按键字段的比特位置
 * @param       desc:报告描述符
 * @param       len:描述符长度
 * @retval      true:找到至少一组包含X/Y的报表布局
 */
static bool hid_parse_report_descriptor(const uint8_t *desc, size_t len)
{
    hid_desc_ctx_t ctx = { 0 };
    hid_mouse_layout_t *layout;

    s_ctx.layouts.parse.count = 0;
    s_ctx.layouts.parse.has_report_id = false;

    layout = hid_parse_layout_get(0);
    if (layout == NULL)
    {
        return false;
    }

    for (size_t i = 0; i < len;)
    {
        uint8_t prefix = desc[i++];
        uint8_t data_size;
        uint32_t uval = 0;

        if (prefix == 0xFE)                 /* 长条目: 跳过 */
        {
            if (i + 2 > len)
            {
                break;
            }
            uint8_t item_size = desc[i];
            i += 2;
            if (i + item_size > len)
            {
                break;
            }
            i += item_size;
            continue;
        }

        data_size = prefix & 0x03;
        if (data_size == 3)
        {
            data_size = 4;
        }

        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = (prefix >> 4) & 0x0F;

        if (i + data_size > len)
        {
            break;
        }
        for (uint8_t b = 0; b < data_size; b++)
        {
            uval |= (uint32_t)desc[i + b] << (8 * b);
        }
        i += data_size;

        /* 带符号扩展, 用于Logical Minimum等 */
        int32_t ival = (int32_t)uval;
        if (data_size > 0 && data_size < 4)
        {
            uint32_t sign_bit = 1u << (data_size * 8 - 1);
            if (uval & sign_bit)
            {
                ival = (int32_t)(uval | (~0u << (data_size * 8)));
            }
        }

        if (type == 0x00)                   /* Main */
        {
            if (tag == 0x8 && layout != NULL)   /* Input */
            {
                hid_desc_handle_input(&ctx, layout, uval);
            }
            else
            {
                /* Collection/Output/Feature/End Collection: 只清局部条目 */
                ctx.usage_count = 0;
                ctx.has_usage_range = false;
            }
        }
        else if (type == 0x01)              /* Global */
        {
            switch (tag)
            {
                case 0x0: ctx.usage_page = uval; break;
                case 0x1: ctx.logical_min = ival; break;
                case 0x7: ctx.report_size = uval; break;
                case 0x8:               /* Report ID: 每种ID的报表单独编号 */
                    s_ctx.layouts.parse.has_report_id = true;
                    layout = hid_parse_layout_get((uint8_t)uval);
                    ctx.bit_offset = 0;
                    break;
                case 0x9: ctx.report_count = uval; break;
                case 0xA:               /* Push */
                    if (ctx.stack_depth < (int)(sizeof(ctx.stack) / sizeof(ctx.stack[0])))
                    {
                        ctx.stack[ctx.stack_depth].page = ctx.usage_page;
                        ctx.stack[ctx.stack_depth].size = ctx.report_size;
                        ctx.stack[ctx.stack_depth].count = ctx.report_count;
                        ctx.stack[ctx.stack_depth].min = ctx.logical_min;
                        ctx.stack_depth++;
                    }
                    break;
                case 0xB:               /* Pop */
                    if (ctx.stack_depth > 0)
                    {
                        ctx.stack_depth--;
                        ctx.usage_page = ctx.stack[ctx.stack_depth].page;
                        ctx.report_size = ctx.stack[ctx.stack_depth].size;
                        ctx.report_count = ctx.stack[ctx.stack_depth].count;
                        ctx.logical_min = ctx.stack[ctx.stack_depth].min;
                    }
                    break;
                default: break;
            }
        }
        else if (type == 0x02)              /* Local */
        {
            switch (tag)
            {
                case 0x0:
                    if (ctx.usage_count < HID_MAX_USAGES)
                    {
                        uint32_t usage = uval;
                        uint32_t page = 0;

                        if (data_size == 4)
                        {
                            /* 4字节Usage: 高16位是Usage Page */
                            page = uval >> 16;
                            usage = uval & 0xFFFF;
                        }

                        ctx.usages[ctx.usage_count] = usage;
                        ctx.usage_pages[ctx.usage_count] = page;
                        ctx.usage_count++;
                    }
                    break;
                case 0x1:
                    ctx.usage_min = (data_size == 4) ? (uval & 0xFFFF) : uval;
                    ctx.has_usage_range = true;
                    break;
                case 0x2:
                    ctx.usage_max = (data_size == 4) ? (uval & 0xFFFF) : uval;
                    ctx.has_usage_range = true;
                    break;
                default: break;
            }
        }
    }

    /* 丢弃没有同时找到X/Y的布局(如键盘接口的描述符) */
    hid_layout_table_t *parse = &s_ctx.layouts.parse;
    int kept = 0;

    for (int i = 0; i < parse->count; i++)
    {
        if (parse->items[i].has_x && parse->items[i].has_y)
        {
            parse->items[kept++] = parse->items[i];
        }
    }
    parse->count = kept;

    if (parse->count == 0)
    {
        return false;       /* 保留原有的有效布局 */
    }

    portENTER_CRITICAL(&s_ctx.layouts.lock);
    s_ctx.layouts.live = *parse;
    portEXIT_CRITICAL(&s_ctx.layouts.lock);

    return true;
}

/* 按比特偏移取出无符号字段值(LSB优先) */
static uint32_t hid_get_bits(const uint8_t *data, int len_bytes, uint16_t bit_off, uint8_t bit_size)
{
    uint32_t value = 0;

    for (uint8_t i = 0; i < bit_size && i < 32; i++)
    {
        uint32_t bit = (uint32_t)bit_off + i;

        if ((int)(bit / 8) >= len_bytes)
        {
            return value;
        }

        value |= (uint32_t)((data[bit / 8] >> (bit % 8)) & 0x01) << i;
    }

    return value;
}

/* 取出字段并按需要做符号扩展 */
static int32_t hid_get_field(const uint8_t *data, int len_bytes, uint16_t bit_off,
                             uint8_t bit_size, bool is_signed)
{
    uint32_t value = hid_get_bits(data, len_bytes, bit_off, bit_size);

    if (is_signed && bit_size > 0 && bit_size < 32 &&
        (value & (1u << (bit_size - 1))))
    {
        value |= ~0u << bit_size;
    }

    return (int32_t)value;
}

/* 布局需要的最小报表体字节数 */
static int hid_layout_min_bytes(const hid_mouse_layout_t *layout)
{
    uint16_t end = 0;

    if (layout->has_x)
    {
        uint16_t e = layout->x_bit + layout->x_size;
        if (e > end)
        {
            end = e;
        }
    }
    if (layout->has_y)
    {
        uint16_t e = layout->y_bit + layout->y_size;
        if (e > end)
        {
            end = e;
        }
    }
    if (layout->buttons_num > 0)
    {
        uint16_t e = layout->buttons_bit + layout->buttons_num;
        if (e > end)
        {
            end = e;
        }
    }

    return (end + 7) / 8;
}

/**
 * @brief       根据报文首字节匹配Report ID, 并返回去掉ID后的报表体
 * @param       data:原始报表
 * @param       length:原始报表长度
 * @param       out:输出的布局副本
 * @param       body:输出报表体(去掉Report ID)
 * @param       body_len:输出报表体长度
 * @retval      true:找到匹配的布局
 */
static bool hid_find_layout(const uint8_t *data, int length, hid_mouse_layout_t *out,
                            const uint8_t **body, int *body_len)
{
    bool found = false;

    portENTER_CRITICAL(&s_ctx.layouts.lock);
    const hid_layout_table_t *table = &s_ctx.layouts.live;

    if (table->count > 0)
    {
        if (table->has_report_id)
        {
            if (length >= 1)
            {
                for (int i = 0; i < table->count; i++)
                {
                    if (table->items[i].report_id == data[0])
                    {
                        *out = table->items[i];
                        *body = data + 1;
                        *body_len = length - 1;
                        found = true;
                        break;
                    }
                }
            }
        }
        else
        {
            *out = table->items[0];
            *body = data;
            *body_len = length;
            found = true;
        }
    }
    portEXIT_CRITICAL(&s_ctx.layouts.lock);

    /* 报文太短(如键盘接口的报文)时不解析, 避免误判成鼠标位移 */
    if (found && *body_len < hid_layout_min_bytes(out))
    {
        found = false;
    }

    return found;
}

/**
 * @brief       鼠标报表处理: 按描述符解析出的布局提取位移/按键
 * @param       data:输入报表数据
 * @param       length:报表长度
 * @retval      无
 */
static void hid_host_mouse_report_callback(const uint8_t *const data, const int length)
{
    const uint8_t *body = data;
    int body_len = length;
    hid_mouse_layout_t layout;
    int32_t dx = 0;
    int32_t dy = 0;
    uint32_t buttons = 0;
    bool have_layout = hid_find_layout(data, length, &layout, &body, &body_len);

    /* 调试: 打印前几次原始报文, 便于确认设备实际格式 */
    if (s_ctx.debug_reports < 5)
    {
        char hex[3 * 12 + 1] = { 0 };
        int n = length < 12 ? length : 12;
        for (int i = 0; i < n; i++)
        {
            snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", data[i]);
        }
        ESP_LOGD(TAG, "report len=%d: %s", length, hex);
        s_ctx.debug_reports++;
    }

    if (have_layout)
    {
        dx = hid_get_field(body, body_len, layout.x_bit, layout.x_size, layout.x_signed);
        dy = hid_get_field(body, body_len, layout.y_bit, layout.y_size, layout.y_signed);

        if (layout.buttons_num > 0)
        {
            buttons = hid_get_bits(body, body_len, layout.buttons_bit, layout.buttons_num);
        }
    }
    else if (length == (int)sizeof(hid_mouse_input_report_boot_t))
    {
        /* 描述符解析失败时退回标准boot鼠标格式(仅限3字节报文) */
        const hid_mouse_input_report_boot_t *mouse_report = (const hid_mouse_input_report_boot_t *)data;

        dx = mouse_report->x_displacement;
        dy = mouse_report->y_displacement;
        buttons = mouse_report->buttons.val;
    }
    else
    {
        return;
    }

    portENTER_CRITICAL(&s_ctx.state_lock);
    s_ctx.state.dx += dx;
    s_ctx.state.dy += dy;
    s_ctx.state.buttons = (uint8_t)buttons;
    s_ctx.state.pending = true;
    portEXIT_CRITICAL(&s_ctx.state_lock);
}

/**
 * @brief       读取并解析鼠标接口的报告描述符
 * @note        内部是同步控制传输, 必须从非HID回调的任务中调用
 * @param       hid_device_handle:HID接口句柄
 * @retval      true:描述符中包含鼠标X/Y字段
 */
static bool hid_load_mouse_layout(hid_host_device_handle_t hid_device_handle)
{
    hid_host_dev_params_t dev_params;
    int iface_num = -1;

    if (hid_host_device_get_params(hid_device_handle, &dev_params) == ESP_OK)
    {
        iface_num = dev_params.iface_num;
    }

    size_t desc_len = 0;
    uint8_t *desc = hid_host_get_report_descriptor(hid_device_handle, &desc_len);

    if (desc == NULL || desc_len == 0)
    {
        ESP_LOGW(TAG, "iface=%d: get report descriptor failed", iface_num);
        return false;
    }

    ESP_LOGD(TAG, "iface=%d: report descriptor len=%d", iface_num, (int)desc_len);
    ESP_LOG_BUFFER_HEXDUMP(TAG, desc, desc_len, ESP_LOG_DEBUG);

    if (!hid_parse_report_descriptor(desc, desc_len))
    {
        ESP_LOGW(TAG, "iface=%d: no mouse X/Y layout in report descriptor", iface_num);
        return false;
    }

    /* 解析成功后parse已整体提交到live, 内容相同; parse只在本任务中访问 */
    const hid_layout_table_t *table = &s_ctx.layouts.parse;

    for (int i = 0; i < table->count; i++)
    {
        const hid_mouse_layout_t *layout = &table->items[i];

        ESP_LOGI(TAG, "mouse layout id=%d X=%u/%u%s Y=%u/%u%s buttons=%u@%u",
                 layout->report_id,
                 layout->x_bit, layout->x_size, layout->x_signed ? "s" : "u",
                 layout->y_bit, layout->y_size, layout->y_signed ? "s" : "u",
                 layout->buttons_num, layout->buttons_bit);
    }

    return true;
}

/* 交给独立任务处理的接口启动请求 */
typedef struct
{
    hid_host_device_handle_t handle;
    bool load_layout;
    bool stop;              /* 通知任务退出(卸载USB Host前使用) */
} hid_layout_job_t;

/**
 * @brief       接口启动任务
 * @note        hid_host_get_report_descriptor() 是同步控制传输, 其完成回调由
 *              HID后台任务处理; 如果在HID后台任务的设备回调里调用会自己等自己,
 *              必然超时(5秒), 描述符永远拿不到。因此把"取描述符 + 启动接口"
 *              都放到这个独立任务里执行。
 * @param       arg:未使用
 * @retval      无
 */
static void hid_layout_task(void *arg)
{
    hid_layout_job_t job;

    (void)arg;

    while (1)
    {
        if (xQueueReceive(s_ctx.layout_queue, &job, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        if (job.stop)
        {
            break;      /* 卸载流程要求退出, 不能再碰已经失效的句柄 */
        }

        if (job.load_layout)
        {
            hid_load_mouse_layout(job.handle);
        }

        esp_err_t ret = hid_host_device_start(job.handle);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "start HID device failed: %s", esp_err_to_name(ret));
            if (hid_host_device_close(job.handle) != ESP_OK)
            {
                ESP_LOGW(TAG, "close HID device failed");
            }
            continue;
        }

        portENTER_CRITICAL(&s_ctx.state_lock);
        s_ctx.state.connected = true;
        portEXIT_CRITICAL(&s_ctx.state_lock);
    }

    xSemaphoreGive(s_ctx.layout_stopped);
    vTaskDelete(NULL);
}

/**
 * @brief       USB HID Host 接口事件
 * @param       hid_device_handle:设备句柄
 * @param       event:接口事件
 * @param       arg:未使用
 * @retval      无
 */
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                        const hid_host_interface_event_t event,
                                        void *arg)
{
    uint8_t data[64] = { 0 };
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;

    if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK)
    {
        ESP_LOGW(TAG, "get HID device params failed");
        return;
    }

    switch (event)
    {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            if (hid_host_device_get_raw_input_report_data(hid_device_handle,
                                                          data,
                                                          sizeof(data),
                                                          &data_length) != ESP_OK)
            {
                ESP_LOGW(TAG, "get raw input report failed");
                break;
            }

            portENTER_CRITICAL(&s_ctx.state_lock);
            bool has_mouse_iface = s_ctx.state.mouse_iface;
            portEXIT_CRITICAL(&s_ctx.state_lock);

            if (HID_PROTOCOL_MOUSE == dev_params.proto)
            {
                hid_host_mouse_report_callback(data, (int)data_length);
            }
            else if (HID_PROTOCOL_NONE == dev_params.proto && !has_mouse_iface)
            {
                /* 没有标准鼠标接口时, 兼容proto=0的非标准鼠标;
                 * 复合设备(鼠标+键盘+厂商接口)里若已有鼠标接口, 忽略其它接口,
                 * 避免键盘/媒体键报文被误当作鼠标位移。
                 * 解析器会按报文长度/Report ID过滤, 不匹配的报文直接丢弃 */
                portENTER_CRITICAL(&s_ctx.state_lock);
                s_ctx.state.connected = true;
                portEXIT_CRITICAL(&s_ctx.state_lock);
                hid_host_mouse_report_callback(data, (int)data_length);
            }
            else
            {
                ESP_LOGD(TAG, "ignore report: proto=%d, length=%d",
                         dev_params.proto, (int)data_length);
            }
            break;

        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HID device (%s) disconnected", hid_proto_name_str[dev_params.proto]);

            portENTER_CRITICAL(&s_ctx.state_lock);
            /* 只在本接口就是鼠标(或鼠标兼容接口)时清状态;
             * 复合设备的键盘/厂商接口断开不影响鼠标 */
            bool own_mouse = (HID_PROTOCOL_MOUSE == dev_params.proto);
            bool fallback = (HID_PROTOCOL_NONE == dev_params.proto && !s_ctx.state.mouse_iface);
            if (own_mouse || fallback)
            {
                s_ctx.state.connected = false;
                s_ctx.state.pending = false;
                s_ctx.state.dx = 0;
                s_ctx.state.dy = 0;
                s_ctx.state.buttons = 0;
                s_ctx.state.mouse_iface = false;
            }
            portEXIT_CRITICAL(&s_ctx.state_lock);

            if (hid_host_device_close(hid_device_handle) != ESP_OK)
            {
                ESP_LOGW(TAG, "close HID device failed");
            }
            break;

        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            ESP_LOGW(TAG, "HID device (%s) transfer error", hid_proto_name_str[dev_params.proto]);
            break;

        default:
            break;
    }
}

/**
 * @brief       USB HID Host 设备事件
 * @param       hid_device_handle:设备句柄
 * @param       event:设备事件
 * @param       arg:未使用
 * @retval      无
 */
static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                     const hid_host_driver_event_t event,
                                     void *arg)
{
    if (HID_HOST_DRIVER_EVENT_CONNECTED != event)
    {
        return;
    }

    hid_host_dev_params_t dev_params;
    if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK)
    {
        ESP_LOGW(TAG, "get HID device params failed");
        return;
    }
    ESP_LOGI(TAG, "HID device connected: protocol=%s, sub_class=%d, addr=%d, iface=%d",
             hid_proto_name_str[dev_params.proto], dev_params.sub_class,
             dev_params.addr, dev_params.iface_num);

    const hid_host_device_config_t dev_config = {
        .callback = hid_host_interface_callback,    /* HID接口回调 */
        .callback_arg = NULL
    };

    esp_err_t ret = hid_host_device_open(hid_device_handle, &dev_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "open HID device failed: %s", esp_err_to_name(ret));
        return;
    }

    /*
     * 不发送SET_PROTOCOL/SET_IDLE控制请求。
     * 部分无线鼠标接收器虽然报告为boot鼠标/键盘, 却不响应这些控制传输,
     * 会超时5秒(例程里还会因ESP_ERROR_CHECK直接复位), 并阻塞后续接口枚举。
     * 改为解析报告描述符, 按实际的字段布局解析报文。
     */
    portENTER_CRITICAL(&s_ctx.state_lock);
    bool has_mouse_iface = s_ctx.state.mouse_iface;
    portEXIT_CRITICAL(&s_ctx.state_lock);

    bool load_layout = false;

    if (HID_PROTOCOL_MOUSE == dev_params.proto)
    {
        portENTER_CRITICAL(&s_ctx.state_lock);
        s_ctx.state.mouse_iface = true;
        portEXIT_CRITICAL(&s_ctx.state_lock);
        load_layout = true;
    }
    else if (HID_PROTOCOL_NONE == dev_params.proto && !has_mouse_iface)
    {
        /* 非标准鼠标: 描述符里有X/Y就按描述符解析 */
        load_layout = true;
    }

    /* 描述符请求和接口启动都放到hid_layout_task里做, 不能在本回调中阻塞 */
    hid_layout_job_t job = {
        .handle = hid_device_handle,
        .load_layout = load_layout,
    };

    if (xQueueSend(s_ctx.layout_queue, &job, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "layout queue full, start without descriptor");
        if (hid_host_device_start(hid_device_handle) != ESP_OK)
        {
            hid_host_device_close(hid_device_handle);
        }
    }
}

/**
 * @brief       USB主机库任务: 安装USB Host并处理库事件
 * @param       arg:创建任务时传入的当前任务句柄
 * @retval      无
 */
static void usb_lib_task(void *arg)
{
    /* 主机配置: 使用默认(高速)USB外设, PHY由USB Host库配置 */
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    esp_err_t ret = usb_host_install(&host_config);
    xTaskNotifyGive(arg);       /* 无论成败都唤醒初始化流程, 避免白等1秒 */
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    while (true)
    {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        /* 没有客户端时释放所有设备并退出 */
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            if (usb_host_device_free_all() != ESP_OK)
            {
                ESP_LOGW(TAG, "usb_host_device_free_all failed");
            }
            break;
        }
    }

    vTaskDelay(10);     /* 必须延时 */
    if (usb_host_uninstall() != ESP_OK)
    {
        ESP_LOGW(TAG, "usb_host_uninstall failed");
    }

    if (s_ctx.host_uninstalled != NULL)
    {
        xSemaphoreGive(s_ctx.host_uninstalled);
    }

    vTaskDelete(NULL);
}

esp_err_t usb_hid_mouse_init(void)
{
    if (s_ctx.installed)
    {
        return ESP_OK;
    }

    /* 同步信号量只创建一次; 卸载/重装会复用 */
    if (s_ctx.host_uninstalled == NULL)
    {
        s_ctx.host_uninstalled = xSemaphoreCreateBinary();
        if (s_ctx.host_uninstalled == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_ctx.layout_stopped == NULL)
    {
        s_ctx.layout_stopped = xSemaphoreCreateBinary();
        if (s_ctx.layout_stopped == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    /* 启动任务: 取报告描述符 + 启动接口(不能在HID回调任务里做) */
    if (s_ctx.layout_queue == NULL)
    {
        s_ctx.layout_queue = xQueueCreate(8, sizeof(hid_layout_job_t));
        if (s_ctx.layout_queue == NULL)
        {
            ESP_LOGE(TAG, "create layout queue failed");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_ctx.layout_task == NULL)
    {
        if (xTaskCreate(hid_layout_task, "hid_layout", 4096, NULL, 3,
                        &s_ctx.layout_task) != pdPASS)
        {
            ESP_LOGE(TAG, "create hid_layout task failed");
            return ESP_ERR_NO_MEM;
        }
    }

    /* 创建USB线程 */
    BaseType_t task_created = xTaskCreatePinnedToCore(usb_lib_task,
                                                      "usb_events",
                                                      4096,
                                                      xTaskGetCurrentTaskHandle(),
                                                      2, NULL, 0);
    if (task_created != pdTRUE)
    {
        ESP_LOGE(TAG, "create usb_events task failed");
        return ESP_ERR_NO_MEM;
    }

    /* 等待USB Host安装完成 */
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

    /* HID主机配置 */
    const hid_host_driver_config_t hid_host_driver_config = {
        .create_background_task = true,         /* 创建HID事件后台任务 */
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_callback,
        .callback_arg = NULL
    };

    esp_err_t ret = hid_host_install(&hid_host_driver_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "hid_host_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ctx.installed = true;
    ESP_LOGI(TAG, "USB HID mouse ready");
    return ESP_OK;
}

esp_err_t usb_hid_mouse_deinit(void)
{
    if (!s_ctx.installed)
    {
        return ESP_OK;
    }

    /* 1. 先让布局任务退出, 避免USB卸载后它还在用旧句柄发控制传输 */
    if (s_ctx.layout_task != NULL)
    {
        hid_layout_job_t stop_job = {
            .handle = NULL,
            .load_layout = false,
            .stop = true,
        };

        (void)xQueueSend(s_ctx.layout_queue, &stop_job, pdMS_TO_TICKS(200));

        if (xSemaphoreTake(s_ctx.layout_stopped, pdMS_TO_TICKS(7000)) == pdTRUE)
        {
            s_ctx.layout_task = NULL;
        }
        else
        {
            /* 描述符控制传输最长5秒, 超时说明任务卡住, 强制删除 */
            ESP_LOGW(TAG, "layout task stop timeout, force delete");
            vTaskDelete(s_ctx.layout_task);
            s_ctx.layout_task = NULL;
        }
    }

    /* 2. 注销HID类驱动, usb_lib_task收到NO_CLIENTS后会卸载USB Host */
    esp_err_t ret = hid_host_uninstall();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "hid_host_uninstall failed: %s", esp_err_to_name(ret));
    }

    if (xSemaphoreTake(s_ctx.host_uninstalled, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "usb host uninstall timeout");
    }

    /* 3. 清状态, 丢弃遗留的接口启动请求 */
    if (s_ctx.layout_queue != NULL)
    {
        xQueueReset(s_ctx.layout_queue);
    }

    portENTER_CRITICAL(&s_ctx.state_lock);
    s_ctx.state.connected = false;
    s_ctx.state.pending = false;
    s_ctx.state.mouse_iface = false;
    s_ctx.state.dx = 0;
    s_ctx.state.dy = 0;
    s_ctx.state.buttons = 0;
    portEXIT_CRITICAL(&s_ctx.state_lock);

    s_ctx.debug_reports = 0;
    s_ctx.installed = false;

    ESP_LOGI(TAG, "USB HID mouse deinitialized");
    return ESP_OK;
}

bool usb_hid_mouse_read(usb_hid_mouse_report_t *report)
{
    if (report == NULL)
    {
        return false;
    }

    bool pending;

    portENTER_CRITICAL(&s_ctx.state_lock);
    pending = s_ctx.state.pending;
    if (pending)
    {
        report->x = s_ctx.state.dx;
        report->y = s_ctx.state.dy;
        report->buttons = s_ctx.state.buttons;

        s_ctx.state.dx = 0;
        s_ctx.state.dy = 0;
        s_ctx.state.pending = false;
    }
    portEXIT_CRITICAL(&s_ctx.state_lock);

    return pending;
}

bool usb_hid_mouse_connected(void)
{
    portENTER_CRITICAL(&s_ctx.state_lock);
    bool connected = s_ctx.state.connected;
    portEXIT_CRITICAL(&s_ctx.state_lock);

    return connected;
}
