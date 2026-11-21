/**
 * @file avi.h
 * @brief AVI 容器解析(MJPEG + PCM)
 *
 * 移植自慧勤智远 26_video_player 例程, 只保留解析部分:
 * 读取 RIFF/AVI/hdrl/strl/strf 头, 找到 movi 块并给出视频帧/音频帧标记。
 */

#ifndef __AVI_H
#define __AVI_H

#include <stdint.h>
#include <stdio.h>

/* 单帧数据上限, 也是文件读取缓冲区大小 */
#define AVI_MAX_FRAME_SIZE  (260 * 1024)

/* 错误类型 */
typedef enum
{
    AVI_OK = 0,             /* 成功 */
    AVI_RIFF_ERR,           /* RIFF ID读取失败 */
    AVI_AVI_ERR,            /* AVI  ID读取失败 */
    AVI_LIST_ERR,           /* LIST ID读取失败 */
    AVI_HDRL_ERR,           /* HDRL ID读取失败 */
    AVI_AVIH_ERR,           /* AVIH ID读取失败 */
    AVI_STRL_ERR,           /* STRL ID读取失败 */
    AVI_STRH_ERR,           /* STRH ID读取失败 */
    AVI_STRF_ERR,           /* STRF ID读取失败 */
    AVI_MOVI_ERR,           /* MOVI ID读取失败 */
    AVI_FORMAT_ERR,         /* 格式错误 */
    AVI_STREAM_ERR,         /* 流错误 */
} AVISTATUS;

#define AVI_RIFF_ID         0X46464952
#define AVI_AVI_ID          0X20495641
#define AVI_LIST_ID         0X5453494C
#define AVI_HDRL_ID         0X6C726468      /* 信息块标志 */
#define AVI_MOVI_ID         0X69766F6D      /* 数据块标志 */
#define AVI_STRL_ID         0X6C727473      /* strl标志 */

#define AVI_AVIH_ID         0X68697661      /* avih子块∈AVI_HDRL_ID */
#define AVI_STRH_ID         0X68727473      /* strh(流头)子块∈AVI_STRL_ID */
#define AVI_STRF_ID         0X66727473      /* strf(流格式)子块∈AVI_STRL_ID */
#define AVI_STRD_ID         0X64727473      /* strd子块∈AVI_STRL_ID (可选) */

#define AVI_VIDS_STREAM     0X73646976      /* 视频流 */
#define AVI_AUDS_STREAM     0X73647561      /* 音频流 */

#define AVI_VIDS_FLAG       0X6463          /* 视频流标志 "dc" */
#define AVI_AUDS_FLAG       0X7762          /* 音频流标志 "wb" */

#define AVI_FORMAT_MJPG     0X47504A4D

/* AVI 解析出的关键信息 */
typedef struct
{
    uint32_t SecPerFrame;       /* 视频帧间隔时间(us) */
    uint32_t TotalFrame;        /* 文件总帧数 */
    uint32_t Width;             /* 图像宽 */
    uint32_t Height;            /* 图像高 */
    uint32_t SampleRate;        /* 音频采样率 */
    uint16_t Channels;          /* 声道数 */
    uint16_t AudioBufSize;      /* 音频缓冲区大小 */
    uint16_t AudioType;         /* 音频类型: 0x0001=PCM */
    uint16_t StreamID;          /* 当前流类型 'dc'/'wb' */
    uint32_t StreamSize;        /* 当前流大小(偶数) */
    char *VideoFLAG;            /* 视频帧标记 "00dc"/"01dc" */
    char *AudioFLAG;            /* 音频帧标记 "00wb"/"01wb" */
} AVI_INFO;

extern AVI_INFO g_avix;         /* AVI 文件相关信息 */

typedef struct
{
    uint32_t RiffID;
    uint32_t FileSize;
    uint32_t AviID;
} AVI_HEADER;

typedef struct
{
    uint32_t FrameID;
    uint32_t FrameSize;
} FRAME_HEADER;

typedef struct
{
    uint32_t ListID;
    uint32_t BlockSize;
    uint32_t ListType;
} LIST_HEADER;

typedef struct
{
    uint32_t BlockID;
    uint32_t BlockSize;
    uint32_t SecPerFrame;
    uint32_t MaxByteSec;
    uint32_t PaddingGranularity;
    uint32_t Flags;
    uint32_t TotalFrame;
    uint32_t InitFrames;
    uint32_t Streams;
    uint32_t RefBufSize;
    uint32_t Width;
    uint32_t Height;
    uint32_t Reserved[4];
} AVIH_HEADER;

typedef struct
{
    uint32_t BlockID;
    uint32_t BlockSize;
    uint32_t StreamType;
    uint32_t Handler;
    uint32_t Flags;
    uint16_t Priority;
    uint16_t Language;
    uint32_t InitFrames;
    uint32_t Scale;
    uint32_t Rate;
    uint32_t Start;
    uint32_t Length;
    uint32_t RefBufSize;
    uint32_t Quality;
    uint32_t SampleSize;
    struct
    {
        short Left;
        short Top;
        short Right;
        short Bottom;
    } Frame;
} STRH_HEADER;

typedef struct
{
    uint32_t BmpSize;
    long Width;
    long Height;
    uint16_t Planes;
    uint16_t BitCount;
    uint32_t Compression;
    uint32_t SizeImage;
    long XpixPerMeter;
    long YpixPerMeter;
    uint32_t ClrUsed;
    uint32_t ClrImportant;
} BMP_HEADER;

typedef struct
{
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
} AVIRGBQUAD;

typedef struct
{
    uint32_t BlockID;
    uint32_t BlockSize;
    BMP_HEADER bmiHeader;
    AVIRGBQUAD bmColors[1];
} STRF_BMPHEADER;

typedef struct
{
    uint32_t BlockID;
    uint32_t BlockSize;
    uint16_t FormatTag;
    uint16_t Channels;
    uint32_t SampleRate;
    uint32_t BaudRate;
    uint16_t BlockAlign;
    uint16_t Size;
} STRF_WAVHEADER;

#define MAKEWORD(ptr)   (uint16_t)(((uint16_t)*((uint8_t*)(ptr))<<8)|(uint16_t)*(uint8_t*)((ptr)+1))
#define MAKEDWORD(ptr)  (uint32_t)(((uint16_t)*(uint8_t*)(ptr)|(((uint16_t)*(uint8_t*)(ptr+1))<<8)|\
                                   (((uint16_t)*(uint8_t*)(ptr+2))<<16)|(((uint16_t)*(uint8_t*)(ptr+3))<<24)))

/**
 * @brief       解析 AVI 文件头
 * @param       buf  : 文件起始数据
 * @param       size : 缓冲区中有效字节数
 * @retval      AVI_OK 或错误码
 */
AVISTATUS avi_init(uint8_t *buf, uint32_t size);

/**
 * @brief       在缓冲区中查找 4 字节 ID
 * @param       buf  : 缓冲区
 * @param       size : 缓冲区大小
 * @param       id   : 4 字节 ID 字符串
 * @retval      找到返回偏移; 未找到返回 0
 */
uint32_t avi_srarch_id(uint8_t *buf, uint32_t size, char *id);

/**
 * @brief       解析一个帧头(00dc/00wb 开头)填充 g_avix.StreamID/StreamSize
 * @param       buf: 帧头地址
 * @retval      AVI_OK 或错误码
 */
AVISTATUS avi_get_streaminfo(uint8_t *buf);

#endif /* __AVI_H */
