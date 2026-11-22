#include "mipi_cam.h"
#include "mipi_lcd.h"
#include "touch.h"

static const char *mipi_cam_tag = "mipi_cam";

#define FPS_FUNC_ON   1         /* 帧率显示开关 */
#define ALIGN_UP_BY(num, align) (((num) + ((align) - 1)) & ~((align) - 1))  /* 对齐操作 */

/* 摄像头运行上下文: 预览状态 + 设备句柄 + 帧缓冲 + PPA */
typedef struct
{
    SemaphoreHandle_t preview_req_sem;  /* 预览请求信号量 */
    volatile bool preview_active;       /* 预览进行中 */
    volatile bool preview_exit;         /* 请求退出预览 */
    bool cam_inited;                    /* 摄像头是否已初始化 */
    bool buffers_queued;                /* 帧缓冲是否已在驱动队列中 */

    int video_fd;                       /* 摄像头设备句柄 */
    struct v4l2_format video_format;    /* 视频数据流格式 */
    void *camera_outbuf[2];             /* 摄像头帧缓冲 */
    void *lcd_buffer[2];                /* LCD帧缓冲(双buffer) */
    size_t data_cache_line_size;        /* cache行大小 */
    ppa_client_handle_t ppa_srm_handle; /* PPA SRM客户端 */
} mipi_cam_ctx_t;

static mipi_cam_ctx_t s_cam = {
    .video_fd = -1,
};

static void lcd_cam_task(void *arg);
static esp_err_t camera_queue_all_buffers(void);
static bool touch_pressed(void);
static void camera_display_frame(int index, void *draw_buffer);

static i2c_master_bus_handle_t s_sccb_bus = NULL;   /* 摄像头SCCB总线(ES8311共用) */

/**
 * @brief       获取摄像头SCCB的I2C总线(首次调用时创建)
 * @note        摄像头与ES8311共用 GPIO28/29 这组 I2C, 所以由本模块统一
 *              创建总线, 再分别交给 esp_video(SCCB) 和 ES8311 使用.
 *              参数与Kconfig保持一致: 端口1, SCL=29, SDA=28, 100kHz.
 * @param       无
 * @retval      I2C总线句柄; 创建失败返回NULL
 */
i2c_master_bus_handle_t mipi_cam_sccb_bus(void)
{
    if (s_sccb_bus != NULL)
    {
        return s_sccb_bus;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port    = CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_PORT,
        .sda_io_num  = CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN,
        .scl_io_num  = CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN,
        .clk_source  = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_sccb_bus);
    if (ret != ESP_OK)
    {
        ESP_LOGE(mipi_cam_tag, "create sccb i2c bus failed: %s", esp_err_to_name(ret));
        s_sccb_bus = NULL;
        return NULL;
    }

    return s_sccb_bus;
}

/**
 * @brief       mipi_cam初始化
 * @note        只完成摄像头硬件和视频接口初始化, 不显示画面.
 *              调用mipi_cam_preview_start()后才开始全屏预览.
 * @param       无
 * @retval      ESP_OK:初始化成功; ESP_FAIL:初始化失败
 */
esp_err_t mipi_cam_init(void)
{
    esp_err_t ret = ESP_OK;

    if (s_cam.cam_inited)
    {
        return ESP_OK;
    }

    mipi_dev_bsp_enable_dsi_phy_power();    /* 配置MIPI设备电源 */

    /* 摄像头与ES8311共用GPIO28/29的I2C总线, 由本模块创建后交给esp_video,
     * 这样音频初始化时可以直接复用同一个总线句柄 */
    ret = app_video_main(mipi_cam_sccb_bus());  /* 初始化摄像头硬件和软件接口 */
    if (ret != ESP_OK)
    {
        ESP_LOGE(mipi_cam_tag, "video main init failed with error 0x%x", ret);
        return ESP_FAIL;
    }

    s_cam.video_fd = app_video_open(0);           /* 打开摄像头设备 */
    if (s_cam.video_fd < 0)
    {
        ESP_LOGE(mipi_cam_tag, "video cam open failed");
        return ESP_FAIL;
    }

    ret = app_video_init(s_cam.video_fd, APP_VIDEO_FMT_RGB565);   /* 初始化设备并设置捕获视频格式 */
    if (ret != ESP_OK)
    {
        ESP_LOGE(mipi_cam_tag, "Video cam init failed with error 0x%x", ret);
        return ESP_FAIL;
    }

    memset(&s_cam.video_format, 0, sizeof(s_cam.video_format));
    s_cam.video_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_cam.video_fd, VIDIOC_G_FMT, &s_cam.video_format) != 0)  /* 获取摄像头输出格式 */
    {
        ESP_LOGE(mipi_cam_tag, "get fmt failed");
        return ESP_FAIL;
    }

    /* 获取LCD BUF(双buffer) */
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(lcddev.lcd_panel_handle, 2, &s_cam.lcd_buffer[0], &s_cam.lcd_buffer[1]));
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cam.data_cache_line_size));

    /* CAM BUF设置两个,使用MMAP */
    ESP_ERROR_CHECK(camera_set_bufs(s_cam.video_fd, 2, NULL));    /* 设置CAMERA的帧缓存数量,并加入驱动队列 */
    ESP_ERROR_CHECK(camera_get_bufs(2, &s_cam.camera_outbuf[0])); /* 获取CAMERA的帧缓冲 */
    s_cam.buffers_queued = true;

    /*---------------PPA的设置-SRM------------------*/
    ppa_client_config_t ppa_srm_config = {                  /* 设置客户端的属性 */
        .oper_type             = PPA_OPERATION_SRM,         /* PPA操作类型(已注册的PPA客户只能请求一种) */
        .max_pending_trans_num = 1,                         /* 客户端可以持有的最大PPA事务挂起数量 */
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_srm_config, &s_cam.ppa_srm_handle)); /* 注册PPA客户端 */

    s_cam.preview_req_sem = xSemaphoreCreateBinary();
    if (s_cam.preview_req_sem == NULL)
    {
        ESP_LOGE(mipi_cam_tag, "create preview semaphore failed");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(lcd_cam_task, "lcd cam display", 4096, NULL, 4, NULL, 0) != pdPASS)
    {
        ESP_LOGE(mipi_cam_tag, "create lcd cam task failed");
        return ESP_ERR_NO_MEM;
    }

    s_cam.cam_inited = true;
    return ESP_OK;
}

/**
 * @brief       请求全屏预览
 * @note        请求成功后LVGL应停止刷新(lvgl任务轮询mipi_cam_preview_active()),
 *              摄像头任务把画面直接写入LCD framebuffer, 触摸屏幕任意位置退出预览.
 * @param       无
 * @retval      ESP_OK:请求成功; ESP_ERR_INVALID_STATE:未初始化; ESP_FAIL:请求失败
 */
esp_err_t mipi_cam_preview_start(void)
{
    if (!s_cam.cam_inited || s_cam.preview_req_sem == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_cam.preview_active)
    {
        return ESP_OK;
    }

    s_cam.preview_exit = false;
    s_cam.preview_active = true;

    if (xSemaphoreGive(s_cam.preview_req_sem) != pdTRUE)
    {
        s_cam.preview_active = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief       查询预览状态
 * @param       无
 * @retval      true:预览中; false:未预览
 */
bool mipi_cam_preview_active(void)
{
    return s_cam.preview_active;
}

/**
 * @brief       把已经配置好的两个帧缓冲重新加入驱动队列
 * @param       无
 * @retval      ESP_OK:成功; ESP_FAIL:失败
 */
static esp_err_t camera_queue_all_buffers(void)
{
    for (int i = 0; i < 2; i++)
    {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (ioctl(s_cam.video_fd, VIDIOC_QBUF, &buf) != 0)
        {
            ESP_LOGE(mipi_cam_tag, "failed to queue video frame %d", i);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

/**
 * @brief       扫描触摸屏,判断是否有按下
 * @param       无
 * @retval      true:有触摸按下; false:无
 */
static bool touch_pressed(void)
{
    tp_dev.scan(0);
    return (tp_dev.sta & TP_PRES_DOWN) != 0;
}

/**
 * @brief       摄像头显示任务
 * @note        等待预览请求 -> 开流显示 -> 触摸退出 -> 关流, 循环
 * @param       arg: 未使用
 * @retval      无
 */
static void lcd_cam_task(void *arg)
{
    (void)arg;

    void *draw_buffer = s_cam.lcd_buffer[1];

    while (1)
    {
        if (xSemaphoreTake(s_cam.preview_req_sem, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        if (!s_cam.buffers_queued)
        {
            if (camera_queue_all_buffers() != ESP_OK)
            {
                s_cam.preview_active = false;
                continue;
            }
            s_cam.buffers_queued = true;
        }

        if (camera_stream_start(s_cam.video_fd) != ESP_OK)
        {
            ESP_LOGE(mipi_cam_tag, "failed to start camera stream");
            s_cam.preview_active = false;
            continue;
        }

        /* 给LVGL一点时间完成当前帧,并等待启动按钮的触摸松开,避免立即退出 */
        for (int i = 0; i < 50 && touch_pressed(); i++)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(pdMS_TO_TICKS(50));

#if FPS_FUNC_ON == 1                                    /* FPS_FUNC_ON该宏 打开帧率显示功能 */
        int fps_count = 0;
        int64_t start_time = esp_timer_get_time();
#endif

        while (!s_cam.preview_exit)
        {
            struct v4l2_buffer buf;
            int res = 0;

            memset(&buf, 0, sizeof(buf));
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;   /* 与前面format.type要一致 */
            buf.memory = V4L2_MEMORY_MMAP;              /* 内存使用mmap方式 */

            res = ioctl(s_cam.video_fd, VIDIOC_DQBUF, &buf);  /* 将已经捕获好视频的内存拉出已捕获视频的队列 */
            if (res != 0)
            {
                ESP_LOGE(mipi_cam_tag, "failed to receive video frame");
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            draw_buffer = s_cam.lcd_buffer[0] == draw_buffer ? s_cam.lcd_buffer[1] : s_cam.lcd_buffer[0];  /* 双缓冲情况下,切换buffer */

            camera_display_frame(buf.index, draw_buffer);

            if (ioctl(s_cam.video_fd, VIDIOC_QBUF, &buf) != 0)    /* 将空闲的内存加入可捕获视频的队列 */
            {
                ESP_LOGE(mipi_cam_tag, "failed to free video frame");
            }

            if (touch_pressed())                            /* 触摸屏幕任意位置退出预览 */
            {
                s_cam.preview_exit = true;
            }

#if FPS_FUNC_ON == 1
            fps_count++;
            if (fps_count == 50)
            {
                int64_t end_time = esp_timer_get_time();
                ESP_LOGI(mipi_cam_tag, "fps: %f", 1000000.0 / ((end_time - start_time) / 50.0));
                start_time = end_time;
                fps_count = 0;
            }
#endif
        }

        camera_stream_stop(s_cam.video_fd);       /* 退出预览,关闭视频流 */
        s_cam.buffers_queued = false;             /* STREAMOFF后所有buffer已出队,下次预览需重新入队 */
        s_cam.preview_active = false;             /* 通知LVGL恢复刷新 */
    }
}

/**
 * @brief       使用PPA把摄像头画面裁剪/缩放到LCD帧缓冲
 * @param       index: 摄像头帧缓冲索引
 * @param       draw_buffer: 目标LCD帧缓冲
 * @retval      无
 */
static void camera_display_frame(int index, void *draw_buffer)
{
    esp_err_t ppa_ret = ESP_ERR_NOT_SUPPORTED;

    if (mipidev.id == 0x7796)                                /* 3.5寸 320*480 ST7706屏幕参数 */
    {
        ppa_srm_oper_config_t oper_config = {
            .in.buffer          = s_cam.camera_outbuf[index],       /* 输入图片数据源 */
            .in.pic_w           = s_cam.video_format.fmt.pix.width, /* 输入图片的宽度 */
            .in.pic_h           = s_cam.video_format.fmt.pix.height,/* 输入图片的高度 */
            .in.block_w         = 320,                        /* 输入块的宽度 */
            .in.block_h         = 480,                        /* 输入块的高度 */
            .in.block_offset_x  = 240,                        /* 输入块的X方向偏移(800x800居中裁剪) */
            .in.block_offset_y  = 160,                        /* 输入块的Y方向偏移(800x800居中裁剪) */
            .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,  /* 输入图片的色彩模式 */

            .out.buffer         = draw_buffer,                /* 输出块的数据源 */
            .out.buffer_size    = ALIGN_UP_BY(lcddev.height * lcddev.width * 16 / 8, s_cam.data_cache_line_size),   /* 数据源大小 */
            .out.pic_w          = lcddev.width,               /* 输出块的宽度(由输入的宽度、缩放因子和旋转角度决定) */
            .out.pic_h          = lcddev.height,              /* 输出块的高度(由输入的宽度、缩放因子和旋转角度决定) */
            .out.block_offset_x = 0,                          /* 输出图片的X方向偏移 */
            .out.block_offset_y = 0,                          /* 输出图片的Y方向偏移 */
            .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,  /* 输出图片的色彩模式 */
            .rotation_angle     = PPA_SRM_ROTATION_ANGLE_0,   /* 输出图片的旋转角度 */
            .scale_x            = 1.0f,                       /* 输出图片的X轴缩放因子 */
            .scale_y            = 1.0f,                       /* 输出图片的Y轴缩放因子 */
            .mirror_x           = false,                      /* 输出图片的X轴镜像选择 */
            .mode               = PPA_TRANS_MODE_BLOCKING,    /* PPA操作执行方式 */
        };
        ppa_ret = ppa_do_scale_rotate_mirror(s_cam.ppa_srm_handle, &oper_config);
    }
    else if (mipidev.id == 0x7102)                           /* 4.3寸 480*800 ST7102屏幕参数 */
    {
        ppa_srm_oper_config_t oper_config = {
            .in.buffer          = s_cam.camera_outbuf[index],
            .in.pic_w           = s_cam.video_format.fmt.pix.width,
            .in.pic_h           = s_cam.video_format.fmt.pix.height,
            .in.block_w         = 800,
            .in.block_h         = 480,
            .in.block_offset_x  = 112,
            .in.block_offset_y  = 60,
            .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,

            .out.buffer         = draw_buffer,
            .out.buffer_size    = ALIGN_UP_BY(lcddev.height * lcddev.width * 16 / 8, s_cam.data_cache_line_size),
            .out.pic_w          = lcddev.width,
            .out.pic_h          = lcddev.height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle     = PPA_SRM_ROTATION_ANGLE_90,
            .scale_x            = 1.0f,
            .scale_y            = 1.0f,
            .mirror_x           = false,
            .mode               = PPA_TRANS_MODE_BLOCKING,
        };
        ppa_ret = ppa_do_scale_rotate_mirror(s_cam.ppa_srm_handle, &oper_config);
    }
    else if (mipidev.id == 0x9881d)                          /* 5寸720P ILI9881D屏幕参数 */
    {
        ppa_srm_oper_config_t oper_config = {
            .in.buffer          = s_cam.camera_outbuf[index],
            .in.pic_w           = s_cam.video_format.fmt.pix.width,
            .in.pic_h           = s_cam.video_format.fmt.pix.height,
            .in.block_w         = 1024,
            .in.block_h         = 576,
            .in.block_offset_x  = 0,
            .in.block_offset_y  = 12,
            .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,

            .out.buffer         = draw_buffer,
            .out.buffer_size    = ALIGN_UP_BY(lcddev.height * lcddev.width * 16 / 8, s_cam.data_cache_line_size),
            .out.pic_w          = lcddev.width,
            .out.pic_h          = lcddev.height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle     = PPA_SRM_ROTATION_ANGLE_90,
            .scale_x            = 1.25f,
            .scale_y            = 1.25f,
            .mirror_x           = false,
            .mode               = PPA_TRANS_MODE_BLOCKING,
        };
        ppa_ret = ppa_do_scale_rotate_mirror(s_cam.ppa_srm_handle, &oper_config);
    }
    else if (mipidev.id == 0x7703)                           /* 5.5寸720P ST7703屏幕参数 */
    {
        ppa_srm_oper_config_t oper_config = {
            .in.buffer          = s_cam.camera_outbuf[index],
            .in.pic_w           = s_cam.video_format.fmt.pix.width,
            .in.pic_h           = s_cam.video_format.fmt.pix.height,
            .in.block_w         = 1024,
            .in.block_h         = 576,
            .in.block_offset_x  = 0,
            .in.block_offset_y  = 12,
            .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,

            .out.buffer         = draw_buffer,
            .out.buffer_size    = ALIGN_UP_BY(lcddev.height * lcddev.width * 16 / 8, s_cam.data_cache_line_size),
            .out.pic_w          = lcddev.width,
            .out.pic_h          = lcddev.height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle     = PPA_SRM_ROTATION_ANGLE_90,
            .scale_x            = 1.25f,
            .scale_y            = 1.25f,
            .mirror_x           = false,
            .mode               = PPA_TRANS_MODE_BLOCKING,
        };
        ppa_ret = ppa_do_scale_rotate_mirror(s_cam.ppa_srm_handle, &oper_config);
    }
    else if (mipidev.id == 0x7707)                           /* 6.86寸 480*1280 FL7707N屏幕参数 */
    {
        ppa_srm_oper_config_t oper_config = {
            .in.buffer          = s_cam.camera_outbuf[index],
            .in.pic_w           = s_cam.video_format.fmt.pix.width,
            .in.pic_h           = s_cam.video_format.fmt.pix.height,
            .in.block_w         = 1024,
            .in.block_h         = 480,
            .in.block_offset_x  = 0,
            .in.block_offset_y  = 60,
            .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,

            .out.buffer         = draw_buffer,
            .out.buffer_size    = ALIGN_UP_BY(lcddev.height * lcddev.width * 16 / 8, s_cam.data_cache_line_size),
            .out.pic_w          = lcddev.width,
            .out.pic_h          = lcddev.height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle     = PPA_SRM_ROTATION_ANGLE_90,
            .scale_x            = 1.25f,
            .scale_y            = 1.0f,
            .mirror_x           = false,
            .mode               = PPA_TRANS_MODE_BLOCKING,
        };
        ppa_ret = ppa_do_scale_rotate_mirror(s_cam.ppa_srm_handle, &oper_config);
    }
    else if (mipidev.id == 0x79007)                          /* 7寸 1024*600 EK79007屏幕参数 */
    {
        ppa_srm_oper_config_t oper_config = {
            .in.buffer          = s_cam.camera_outbuf[index],
            .in.pic_w           = s_cam.video_format.fmt.pix.width,
            .in.pic_h           = s_cam.video_format.fmt.pix.height,
            .in.block_w         = 1024,
            .in.block_h         = 600,
            .in.block_offset_x  = 0,
            .in.block_offset_y  = 0,
            .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,

            .out.buffer         = draw_buffer,
            .out.buffer_size    = ALIGN_UP_BY(lcddev.height * lcddev.width * 16 / 8, s_cam.data_cache_line_size),
            .out.pic_w          = lcddev.width,
            .out.pic_h          = lcddev.height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle     = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x            = 1.0f,
            .scale_y            = 1.0f,
            .mirror_x           = false,
            .mode               = PPA_TRANS_MODE_BLOCKING,
        };
        ppa_ret = ppa_do_scale_rotate_mirror(s_cam.ppa_srm_handle, &oper_config);
    }
    else if (mipidev.id == 0x9881c8)                         /* 8寸800P ILI9881C屏幕参数 */
    {
        ppa_srm_oper_config_t oper_config = {
            .in.buffer          = s_cam.camera_outbuf[index],
            .in.pic_w           = s_cam.video_format.fmt.pix.width,
            .in.pic_h           = s_cam.video_format.fmt.pix.height,
            .in.block_w         = 800,
            .in.block_h         = 512,
            .in.block_offset_x  = 112,
            .in.block_offset_y  = 44,
            .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,

            .out.buffer         = draw_buffer,
            .out.buffer_size    = ALIGN_UP_BY(lcddev.height * lcddev.width * 16 / 8, s_cam.data_cache_line_size),
            .out.pic_w          = lcddev.width,
            .out.pic_h          = lcddev.height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle     = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x            = 1.0f,
            .scale_y            = 2.5f,
            .mirror_x           = false,
            .mode               = PPA_TRANS_MODE_BLOCKING,
        };
        ppa_ret = ppa_do_scale_rotate_mirror(s_cam.ppa_srm_handle, &oper_config);
    }
    else if (mipidev.id == 0x9881c10)                        /* 10.1寸800P ILI9881C屏幕参数 */
    {
        ppa_srm_oper_config_t oper_config = {
            .in.buffer          = s_cam.camera_outbuf[index],
            .in.pic_w           = s_cam.video_format.fmt.pix.width,
            .in.pic_h           = s_cam.video_format.fmt.pix.height,
            .in.block_w         = 800,
            .in.block_h         = 512,
            .in.block_offset_x  = 112,
            .in.block_offset_y  = 44,
            .in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565,

            .out.buffer         = draw_buffer,
            .out.buffer_size    = ALIGN_UP_BY(lcddev.height * lcddev.width * 16 / 8, s_cam.data_cache_line_size),
            .out.pic_w          = lcddev.width,
            .out.pic_h          = lcddev.height,
            .out.block_offset_x = 0,
            .out.block_offset_y = 0,
            .out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
            .rotation_angle     = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x            = 1.0f,
            .scale_y            = 2.5f,
            .mirror_x           = false,
            .mode               = PPA_TRANS_MODE_BLOCKING,
        };
        ppa_ret = ppa_do_scale_rotate_mirror(s_cam.ppa_srm_handle, &oper_config);
    }

    if (ppa_ret == ESP_OK)
    {
        /* 提交帧缓冲: 刷新cache并让DPI切换到该缓冲(LCD驱动检测到指针在帧缓冲内时只做cache回写) */
        esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, 0, 0,
                                  lcddev.width, lcddev.height, draw_buffer);
    }
}
