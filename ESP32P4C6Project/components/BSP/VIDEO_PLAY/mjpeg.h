/**
 * @file mjpeg.h
 * @brief MJPEG 帧解码 + 全屏显示(硬件 JPEG 解码器 + 最近邻缩放)
 *
 * 全屏黑边居中显示, 不预留 UI 区域。
 */

#ifndef __MJPEG_H
#define __MJPEG_H

#include <stdint.h>

/**
 * @brief       初始化 JPEG 解码器并计算全屏显示参数
 * @param       width      : 视频源宽度
 * @param       height     : 视频源高度
 * @param       malloc_size: 输出缓冲区实际分配大小
 * @retval      解码输出缓冲区指针; 失败返回 NULL
 */
uint8_t *mjpegdec_init(uint32_t width, uint32_t height, uint32_t *malloc_size);

/**
 * @brief       解码一帧 JPEG 并显示(自动缩放/黑边居中)
 * @param       inbuf      : JPEG 压缩数据
 * @param       inbuf_size : 压缩数据大小
 * @param       outbuf     : mjpegdec_init 返回的解码缓冲区
 * @param       outbuf_size: 解码缓冲区大小
 * @param       width      : 视频源宽度
 * @param       height     : 视频源高度
 * @retval      0:成功; 1:错误帧/解码失败
 */
uint8_t mjpegdec_decode(uint8_t *inbuf, uint32_t inbuf_size, uint8_t *outbuf,
                        uint32_t outbuf_size, uint32_t width, uint32_t height);

/**
 * @brief       卸载 JPEG 解码器并释放缓冲区
 * @param       buf: mjpegdec_init 返回的解码缓冲区
 * @retval      无
 */
void mjpegdec_free(uint8_t *buf);

#endif /* __MJPEG_H */
