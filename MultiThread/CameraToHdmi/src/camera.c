#include "../include/camera.h"

int camera_init(CameraContext *ctx, const char *dev_name, uint32_t w, uint32_t h, 
                uint32_t count, uint32_t pixel_format, uint32_t buf_type)
{
    memset(ctx, 0, sizeof(CameraContext));
    strncpy(ctx->dev_name, dev_name, sizeof(ctx->dev_name) - 1);

    ctx->fd = open(dev_name, O_RDWR | O_CLOEXEC);
    if (ctx->fd < 0) {
        perror("摄像头打开失败");
        return -1;
    }

    ctx->width = w;
    ctx->height = h;
    ctx->buffer_count = count;
    ctx->buf_type = buf_type;
    ctx->pixel_format = pixel_format;

    // 1. 设置格式
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = ctx->buf_type;
    fmt.fmt.pix_mp.width = ctx->width;
    fmt.fmt.pix_mp.height = ctx->height;
    fmt.fmt.pix_mp.pixelformat = ctx->pixel_format;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE; 
    fmt.fmt.pix_mp.num_planes = 1;

    if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("V4L2 格式设置失败");
        return -1;
    }

    // 2. 申请缓冲区
    struct v4l2_requestbuffers rqp = {0};
    rqp.count = ctx->buffer_count;
    rqp.type = ctx->buf_type;
    rqp.memory = V4L2_MEMORY_MMAP;

    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &rqp) < 0) {
        perror("V4L2 申请缓冲区失败");
        return -1;
    }

    if (rqp.count == 0) {
        fprintf(stderr, "V4L2 分配了 0 个 Buffer\n");
        return -1;
    }
    ctx->buffer_count = rqp.count; 

    ctx->buffers = calloc(ctx->buffer_count, sizeof(CameraBuffer));
    if (!ctx->buffers) {
        perror("分配内存失败");
        return -1;
    }

    // 3. 映射与导出 FD
    for (uint32_t i = 0; i < ctx->buffer_count; i++) {
        struct v4l2_buffer buf = {0};
        struct v4l2_plane planes[1] = {0};
        buf.type = ctx->buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = 1;
        buf.m.planes = planes;

        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF 失败");
            break;
        }

        ctx->buffers[i].length = planes[0].length;
        ctx->buffers[i].start = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE, 
                                     MAP_SHARED, ctx->fd, planes[0].m.mem_offset);
        
        if (ctx->buffers[i].start == MAP_FAILED) {
            perror("mmap 失败");
            return -1; 
        }
        
        // 导出 DMA FD
        struct v4l2_exportbuffer expbuf = {0};
        expbuf.type = ctx->buf_type;
        expbuf.index = i;
        if (ioctl(ctx->fd, VIDIOC_EXPBUF, &expbuf) == 0) {
            ctx->buffers[i].dma_fd = expbuf.fd;
        } else {
            perror("VIDIOC_EXPBUF 失败");
            return -1;
        }
        
        // 预入队空缓冲区
        if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) == 0) {
            ctx->buffers[i].is_queued = true; 
        } else {
            perror("初始 QBUF 失败");
            return -1;
        }
    }

    ctx->is_initialized = true;
    return 0;
}
int camera_start(CameraContext *ctx);
int camera_get_frame(CameraContext *ctx, Frame *frame);
int camera_put_frame(CameraContext *ctx, const Frame *frame); // 归还 Buffer
int camera_stop(CameraContext *ctx);
int camera_deinit(CameraContext *ctx)
{
    if (!ctx || !ctx->is_initialized) return 0; // 防御性检查，支持重复调用

    if (ctx->fd > 0) {
        enum v4l2_buf_type type = ctx->buf_type;
        ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
    }

    if (ctx->buffers) {
        for (uint32_t i = 0; i < ctx->buffer_count; i++) {
            CameraBuffer *buf = &ctx->buffers[i];
            
            // 解除 mmap
            if (buf->start && buf->start != MAP_FAILED) {
                munmap(buf->start, buf->length);
                buf->start = NULL;
            }
            // 关闭 DMA FD
            if (buf->dma_fd > 0) {
                close(buf->dma_fd);
                buf->dma_fd = -1;
            }
        }
        // 释放动态分配的结构体数组
        free(ctx->buffers);
        ctx->buffers = NULL;
    }

    if (ctx->fd > 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    ctx->is_initialized = false;
    return 0;
}