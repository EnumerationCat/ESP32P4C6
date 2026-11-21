/**
 * @file videoplay.h
 * @brief AVI(MJPEG) 视频播放器: 异步播放单个文件
 *
 * 用法(参考相机预览的独占屏幕方式):
 *   1. 调用 videoplay_play(path) 启动后台播放任务;
 *   2. LVGL 任务轮询 videoplay_active(), 为 true 时暂停刷新,
 *      播放器把画面直接写入 LCD 帧缓冲;
 *   3. 触摸屏幕任意位置(或调用 videoplay_stop)退出播放;
 *   4. 播放结束后 videoplay_active() 返回 false, LVGL 重新绘制界面。
 *
 * 只移植了视频通路; 参考工程里的 ES8311/I2S 音频输出在本板 BSP 中不存在,
 * 音频帧会被跳过。
 */

#ifndef __VIDEOPLAY_H
#define __VIDEOPLAY_H

#include <stdbool.h>
#include "esp_err.h"

#define VIDEOPLAY_DIR   "/User/VIDEO"   /* 视频目录(SD卡根目录下的 VIDEO) */

/**
 * @brief       异步播放一个 AVI 文件
 * @param       path: 文件完整路径(如 "/User/VIDEO/a.avi")
 * @retval      ESP_OK:已启动; ESP_ERR_INVALID_STATE:正在播放; 其他:启动失败
 */
esp_err_t videoplay_play(const char *path);

/**
 * @brief       播放器是否正在独占屏幕播放
 * @retval      true:播放中
 */
bool videoplay_active(void);

/**
 * @brief       请求停止当前播放(异步, 任务退出后 active 变 false)
 * @param       无
 * @retval      无
 */
void videoplay_stop(void);

#endif /* __VIDEOPLAY_H */
