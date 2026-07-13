#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#define _GNU_SOURCE 

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdbool.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "common.h"

#define DRM_BUFFER_COUNT    2 
#define DRM_BPP             32
// 内部管理的单个 DRM Dumb Buffer 结构
typedef struct {
    uint32_t handle;     // DRM handle (内部对象句柄)
    uint32_t fb_id;      // Framebuffer ID (供 DRM 设置显示使用)
    uint32_t pitch;      // 步长 (一行像素占用的字节数)
    uint64_t size;       // 显存总大小
    int dma_fd;          // 导出的 DMA FD (供 RGA 写入目标)
    void *vaddr;         // 映射到用户空间的地址 (备用/调试用)
} DrmBuffer;

// 显示上下文 (对外视为不透明对象)
typedef struct DisplayContext {
    // 1. 设备基础信息
    int fd;
    char dev_name[64];      // 如 "/dev/dri/card0"

    // 2. 屏幕与输出格式
    uint32_t width;
    uint32_t height;
    uint32_t bpp;           // 位深 (如 32)
    uint32_t pixel_format;  // DRM_FORMAT_XRGB8888 或 RK_FORMAT_BGRA_8888

    // 3. DRM 核心对象 ID
    uint32_t crtc_id;
    uint32_t connector_id;
    drmModeModeInfo mode;
    drmModeCrtc *saved_crtc; // 保存启动前的屏幕状态，用于程序退出时完美恢复

    // 4. 双缓冲管理 (Ping-Pong)
    DrmBuffer buffers[DRM_BUFFER_COUNT];
    int front_buf_idx;       // 当前正在屏幕上显示的 Buffer 索引
    int back_buf_idx;        // 当前可供 RGA 绘制的后台 Buffer 索引

    // 5. 运行状态
    bool is_initialized;

} DisplayContext;


int display_init(DisplayContext *ctx, const char *dev_name);
// 获取后台 Buffer 的 Frame 信息，以便传给 RGA
int display_get_back_frame(DisplayContext *ctx, Frame *drm_frame);
// 执行 Page Flip (翻页)，将刚刚 RGA 渲染完的后台 Buffer 切换到前台显示
int display_show(DisplayContext *ctx); 
int display_deinit(DisplayContext *ctx);

#endif // _DISPLAY_H_