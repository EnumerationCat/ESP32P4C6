/**
 * @file avi.c
 * @brief AVI 容器解析(MJPEG + PCM)
 *
 * 移植自慧勤智远 26_video_player 例程。
 */

#include "avi.h"
#include "esp_log.h"

AVI_INFO g_avix;                                      /* AVI 文件相关信息 */
static char *const AVI_VIDS_FLAG_TBL[2] = {"00dc", "01dc"};  /* 视频帧标记 */
static char *const AVI_AUDS_FLAG_TBL[2] = {"00wb", "01wb"};  /* 音频帧标记 */

/**
 * @brief       解析 AVI 文件头, 提取帧率/分辨率/音频参数/流标记
 * @param       buf  : 文件起始数据
 * @param       size : 缓冲区中有效字节数
 * @retval      AVI_OK 或错误码
 */
AVISTATUS avi_init(uint8_t *buf, uint32_t size)
{
    uint16_t offset;
    uint8_t *tbuf;
    AVI_HEADER *aviheader;
    LIST_HEADER *listheader;
    AVIH_HEADER *avihheader;
    STRH_HEADER *strhheader;

    STRF_BMPHEADER *bmpheader;
    STRF_WAVHEADER *wavheader;

    tbuf = buf;
    aviheader = (AVI_HEADER *)buf;
    if (aviheader->RiffID != AVI_RIFF_ID)
    {
        return AVI_RIFF_ERR;
    }

    if (aviheader->AviID != AVI_AVI_ID)
    {
        return AVI_AVI_ERR;
    }

    buf += sizeof(AVI_HEADER);
    listheader = (LIST_HEADER *)(buf);
    if (listheader->ListID != AVI_LIST_ID)
    {
        return AVI_LIST_ERR;
    }

    if (listheader->ListType != AVI_HDRL_ID)
    {
        return AVI_HDRL_ERR;
    }

    buf += sizeof(LIST_HEADER);
    avihheader = (AVIH_HEADER *)(buf);
    if (avihheader->BlockID != AVI_AVIH_ID)
    {
        return AVI_AVIH_ERR;
    }

    g_avix.SecPerFrame = avihheader->SecPerFrame;   /* 帧间隔时间(us) */
    g_avix.TotalFrame = avihheader->TotalFrame;     /* 总帧数 */
    buf += avihheader->BlockSize + 8;
    listheader = (LIST_HEADER *)(buf);
    if (listheader->ListID != AVI_LIST_ID)
    {
        return AVI_LIST_ERR;
    }

    if (listheader->ListType != AVI_STRL_ID)
    {
        return AVI_STRL_ERR;
    }

    strhheader = (STRH_HEADER *)(buf + 12);
    if (strhheader->BlockID != AVI_STRH_ID)
    {
        return AVI_STRH_ERR;
    }

    if (strhheader->StreamType == AVI_VIDS_STREAM)  /* 视频流在前 */
    {
        if (strhheader->Handler != AVI_FORMAT_MJPG)
        {
            return AVI_FORMAT_ERR;  /* 只支持 MJPEG */
        }

        g_avix.VideoFLAG = AVI_VIDS_FLAG_TBL[0];
        g_avix.AudioFLAG = AVI_AUDS_FLAG_TBL[1];
        bmpheader = (STRF_BMPHEADER *)(buf + 12 + strhheader->BlockSize + 8);
        if (bmpheader->BlockID != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;
        }

        g_avix.Width = bmpheader->bmiHeader.Width;
        g_avix.Height = bmpheader->bmiHeader.Height;
        buf += listheader->BlockSize + 8;
        listheader = (LIST_HEADER *)(buf);
        if (listheader->ListID != AVI_LIST_ID)  /* 没有音频流 */
        {
            g_avix.SampleRate = 0;
            g_avix.Channels = 0;
            g_avix.AudioType = 0;
        }
        else
        {
            if (listheader->ListType != AVI_STRL_ID)
            {
                return AVI_STRL_ERR;
            }

            strhheader = (STRH_HEADER *)(buf + 12);
            if (strhheader->BlockID != AVI_STRH_ID)
            {
                return AVI_STRH_ERR;
            }

            if (strhheader->StreamType != AVI_AUDS_STREAM)
            {
                return AVI_FORMAT_ERR;
            }

            wavheader = (STRF_WAVHEADER *)(buf + 12 + strhheader->BlockSize + 8);
            if (wavheader->BlockID != AVI_STRF_ID)
            {
                return AVI_STRF_ERR;
            }

            g_avix.SampleRate = wavheader->SampleRate;
            g_avix.Channels = wavheader->Channels;
            g_avix.AudioType = wavheader->FormatTag;
        }
    }
    else if (strhheader->StreamType == AVI_AUDS_STREAM)     /* 音频流在前 */
    {
        g_avix.VideoFLAG = AVI_VIDS_FLAG_TBL[1];
        g_avix.AudioFLAG = AVI_AUDS_FLAG_TBL[0];
        wavheader = (STRF_WAVHEADER *)(buf + 12 + strhheader->BlockSize + 8);
        if (wavheader->BlockID != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;
        }

        g_avix.SampleRate = wavheader->SampleRate;
        g_avix.Channels = wavheader->Channels;
        g_avix.AudioType = wavheader->FormatTag;
        buf += listheader->BlockSize + 8;
        listheader = (LIST_HEADER *)(buf);
        if (listheader->ListID != AVI_LIST_ID)
        {
            return AVI_LIST_ERR;
        }

        if (listheader->ListType != AVI_STRL_ID)
        {
            return AVI_STRL_ERR;
        }

        strhheader = (STRH_HEADER *)(buf + 12);
        if (strhheader->BlockID != AVI_STRH_ID)
        {
            return AVI_STRH_ERR;
        }

        if (strhheader->StreamType != AVI_VIDS_STREAM)
        {
            return AVI_FORMAT_ERR;
        }

        bmpheader = (STRF_BMPHEADER *)(buf + 12 + strhheader->BlockSize + 8);
        if (bmpheader->BlockID != AVI_STRF_ID)
        {
            return AVI_STRF_ERR;
        }

        if (bmpheader->bmiHeader.Compression != AVI_FORMAT_MJPG)
        {
            return AVI_FORMAT_ERR;
        }

        g_avix.Width = bmpheader->bmiHeader.Width;
        g_avix.Height = bmpheader->bmiHeader.Height;
    }

    offset = avi_srarch_id(tbuf, size, "movi");     /* 查找 movi 块 */
    if (offset == 0)
    {
        return AVI_MOVI_ERR;
    }

    if (g_avix.SampleRate)          /* 有音频流才解析音频帧大小 */
    {
        tbuf += offset;
        offset = avi_srarch_id(tbuf, size, g_avix.AudioFLAG);
        if (offset == 0)
        {
            return AVI_STREAM_ERR;
        }
        tbuf += offset + 4;
        g_avix.AudioBufSize = *((uint16_t *)tbuf);
    }

    ESP_LOGI("avi", "init ok: %lux%lu, %lu us/frame, %lu frames, audio %lu Hz %u ch",
             (unsigned long)g_avix.Width, (unsigned long)g_avix.Height,
             (unsigned long)g_avix.SecPerFrame, (unsigned long)g_avix.TotalFrame,
             (unsigned long)g_avix.SampleRate, g_avix.Channels);

    return AVI_OK;
}

/**
 * @brief       在缓冲区中查找 4 字节 ID
 * @param       buf  : 缓冲区
 * @param       size : 缓冲区大小
 * @param       id   : 4 字节 ID 字符串
 * @retval      找到返回 ID 偏移; 未找到返回 0
 */
uint32_t avi_srarch_id(uint8_t *buf, uint32_t size, char *id)
{
    uint32_t i;
    uint32_t idsize = 0;

    if (size < 4)
    {
        return 0;
    }
    size -= 4;

    for (i = 0; i < size; i++)
    {
        if ((buf[i] == id[0]) &&
            (buf[i + 1] == id[1]) &&
            (buf[i + 2] == id[2]) &&
            (buf[i + 3] == id[3]))
        {
            idsize = MAKEDWORD(buf + i + 4);

            if (idsize > 0X10)
            {
                return i;
            }
        }
    }

    return 0;
}

/**
 * @brief       解析一个帧头, 填充 g_avix.StreamID/StreamSize
 * @param       buf: 帧头地址(00dc/00wb 开头)
 * @retval      AVI_OK 或错误码
 */
AVISTATUS avi_get_streaminfo(uint8_t *buf)
{
    g_avix.StreamID = MAKEWORD(buf + 2);
    g_avix.StreamSize = MAKEDWORD(buf + 4);

    if (g_avix.StreamSize > AVI_MAX_FRAME_SIZE)
    {
        ESP_LOGE("avi", "frame too large: %lu", (unsigned long)g_avix.StreamSize);
        g_avix.StreamSize = 0;
        return AVI_STREAM_ERR;
    }

    if (g_avix.StreamSize % 2)
    {
        g_avix.StreamSize++;    /* 帧大小必须为偶数 */
    }

    if (g_avix.StreamID == AVI_VIDS_FLAG || g_avix.StreamID == AVI_AUDS_FLAG)
    {
        return AVI_OK;
    }

    return AVI_STREAM_ERR;
}
