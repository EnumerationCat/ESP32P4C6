/**
 * @file recorder.h
 * @brief WAV 录音机(只录音, 不做回放)
 *
 * 从参考例程 25_recoding 的 recorder.c 移植:
 *   - I2S RX 直连 ES8311, 16kHz / 2ch / 16bit PCM;
 *   - 文件保存到 SD 卡 /User/RECORDER/REC_xxxxx.wav;
 *   - 开始前会把 I2S 通道强制重建为 16kHz(视频播放可能改过采样率);
 *   - 录音在独立任务中完成, 停止时回填 WAV 头并关闭文件。
 */

#ifndef __RECORDER_H
#define __RECORDER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* 开始录音: 校验SD/编解码器/I2S, 创建目录与文件, 启动录音任务
 * 返回 ESP_OK 表示已开始; 其他值表示失败(未创建任务) */
esp_err_t recorder_start(void);

/* 停止录音: 置停止标志并等待任务写好文件头、关闭文件
 * 未在录音时直接返回 ESP_OK */
esp_err_t recorder_stop(void);

/* 是否正在录音 */
bool recorder_is_recording(void);

/* 已录音时长(秒); 停止后保留最终值, 重新开始录音时清零 */
uint32_t recorder_elapsed_sec(void);

/* 当前/最近一次录音的文件名(仅文件名, 如 "REC_00000.wav"); 未录音过返回空串 */
const char *recorder_current_file_name(void);

/* 异步播放一个录音WAV文件(16bit PCM); 正在录音/播放时返回 ESP_ERR_INVALID_STATE
 * 返回 ESP_OK 表示播放任务已启动, 用 recorder_is_playing() 轮询状态 */
esp_err_t recorder_play(const char *path);

/* 停止播放: 置停止标志并等待播放任务退出; 未在播放时直接返回 ESP_OK */
esp_err_t recorder_play_stop(void);

/* 是否正在播放 */
bool recorder_is_playing(void);

#endif /* __RECORDER_H */
