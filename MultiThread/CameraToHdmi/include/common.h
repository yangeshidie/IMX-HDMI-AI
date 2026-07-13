#ifndef _FRAME_H_
#define _FRAME_H_

#include <stdint.h>

typedef struct {
    int dma_fd;         // V4L2 或 DRM 导出的 DMA 文件描述符
    int width;
    int height;
    int format;         // 例如 V4L2_PIX_FMT_NV12 或 DRM_FORMAT_XRGB8888
    int index;          // 缓冲区索引 (用于 V4L2 QBUF)
    uint64_t timestamp; // 时间戳，用于音视频同步或帧率计算
} Frame;

#endif // _FRAME_H_