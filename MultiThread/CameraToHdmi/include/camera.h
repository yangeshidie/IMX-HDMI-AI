#ifndef _CAMERA_H_
#define _CAMERA_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/videodev2.h>
#include "common.h"

// 内部管理的单个摄像头缓冲区结构
typedef struct {
    void *start;         // mmap 的用户空间虚拟地址 (用于释放)
    size_t length;       // 缓冲区大小
    int dma_fd;          // 导出的 DMA FD (供给 RGA 使用)
    bool is_queued;      // 状态标记：当前是否已入队交给了底层驱动
} CameraBuffer;

// 摄像头上下文 (对外视为不透明对象)
typedef struct CameraContext {
    // 1. 设备基础信息
    int fd;                 // /dev/videoX 的文件描述符
    char dev_name[64];      // 设备节点名称，方便报错打印

    // 2. 格式与配置参数
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;  // 如 V4L2_PIX_FMT_NV12
    uint32_t buf_type;      // 如 V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE

    // 3. 缓冲池管理
    uint32_t buffer_count;  // 申请的 Buffer 数量 (例如 4)
    CameraBuffer *buffers;  // 动态分配的 Buffer 数组

    // 4. 运行状态
    bool is_initialized;    // 是否已完成初始化
    bool is_streaming;      // 是否已经 STREAMON

    // 5. 线程安全 (可选，视具体设计而定)
    // pthread_mutex_t lock; // 如果允许多线程同时调用 camera 的 API，则需要锁

} CameraContext;

int camera_init(CameraContext *ctx, const char *dev_name, int w, int h, int count);
int camera_start(CameraContext *ctx);
int camera_get_frame(CameraContext *ctx, Frame *frame);
int camera_put_frame(CameraContext *ctx, const Frame *frame); // 归还 Buffer
int camera_stop(CameraContext *ctx);
int camera_deinit(CameraContext *ctx);

#endif // _CAMERA_H_