#include "../include/display.h"

int display_init(DisplayContext *ctx, const char *dev_name)
{
    drmModeRes *res = NULL;
    drmModeConnector *conn = NULL;
    drmModeEncoder *enc = NULL;

    memset(ctx, 0, sizeof(DisplayContext));
    strncpy(ctx->dev_name, dev_name, sizeof(ctx->dev_name) - 1);

    ctx->fd = open(dev_name, O_RDWR | O_CLOEXEC);
    if (ctx->fd < 0) {
        perror("DRM 设备打开失败");
        return -1;
    }

    res = drmModeGetResources(ctx->fd);
    if (!res) {
        fprintf(stderr, "无法获取 DRM 资源\n");
        goto err_fd; // 跳转清理
    }

    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(ctx->fd, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            break;
        }
        drmModeFreeConnector(conn);
        conn = NULL;
    }

    if (!conn) {
        fprintf(stderr, "未找到已连接的显示器\n");
        goto err_res;
    }

    ctx->connector_id = conn->connector_id;
    ctx->mode = conn->modes[0];
    ctx->width = ctx->mode.hdisplay;
    ctx->height = ctx->mode.vdisplay;
    ctx->bpp = 32; 

    enc = drmModeGetEncoder(ctx->fd, conn->encoder_id);
    if (!enc) {
        fprintf(stderr, "获取 Encoder 失败\n");
        goto err_conn;
    }
    
    ctx->crtc_id = enc->crtc_id;
    ctx->saved_crtc = drmModeGetCrtc(ctx->fd, ctx->crtc_id);
    drmModeFreeEncoder(enc);

    // === 核心：创建双缓冲 ===
    for (int i = 0; i < DRM_BUFFER_COUNT; i++) {
        DrmBuffer *buf = &ctx->buffers[i];

        struct drm_mode_create_dumb creq = {0};
        creq.width = ctx->width;
        creq.height = ctx->height;
        creq.bpp = ctx->bpp;
        if (ioctl(ctx->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
            perror("创建 DRM Dumb Buffer 失败");
            goto err_bufs; // 跳转清理已创建的 buffers
        }
        
        buf->handle = creq.handle;
        buf->pitch = creq.pitch;
        buf->size = creq.size;

        if (drmModeAddFB(ctx->fd, ctx->width, ctx->height, 24, ctx->bpp, 
                         buf->pitch, buf->handle, &buf->fb_id) < 0) {
            perror("drmModeAddFB 失败");
            goto err_bufs;
        }

        // 优化：建议加上 DRM_CLOEXEC 标志（宏值通常为 O_CLOEXEC 或直接写 O_CLOEXEC）
        if (drmPrimeHandleToFD(ctx->fd, buf->handle, O_CLOEXEC, &buf->dma_fd) < 0) {
            perror("DRM Buffer 导出 DMA FD 失败");
            goto err_bufs;
        }

        struct drm_mode_map_dumb mreq = {0};
        mreq.handle = buf->handle;
        if (ioctl(ctx->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) == 0) {
            buf->vaddr = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, mreq.offset);
            if (buf->vaddr != MAP_FAILED) {
                memset(buf->vaddr, 0, buf->size); // 填 0 即为黑色
            } else {
                buf->vaddr = NULL; // 防止后续清理时 munmap(MAP_FAILED)
            }
        }
    }

    if (drmModeSetCrtc(ctx->fd, ctx->crtc_id, ctx->buffers[0].fb_id, 0, 0, 
                       &ctx->connector_id, 1, &ctx->mode) < 0) {
        perror("drmModeSetCrtc 失败");
        goto err_bufs;
    }

    ctx->front_buf_idx = 0; 
    ctx->back_buf_idx = 1;  

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    ctx->is_initialized = true;
    printf("DRM 显示初始化成功: %dx%d (双缓冲就绪)\n", ctx->width, ctx->height);

    return 0;

err_bufs:
    for (int i = 0; i < DRM_BUFFER_COUNT; i++) {
        DrmBuffer *buf = &ctx->buffers[i];
        if (buf->vaddr) munmap(buf->vaddr, buf->size);
        if (buf->dma_fd > 0) close(buf->dma_fd);
        if (buf->fb_id) drmModeRmFB(ctx->fd, buf->fb_id);
        if (buf->handle) {
            struct drm_mode_destroy_dumb dreq = { .handle = buf->handle };
            ioctl(ctx->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        }
    }
    if (ctx->saved_crtc) drmModeFreeCrtc(ctx->saved_crtc);
err_conn:
    drmModeFreeConnector(conn);
err_res:
    drmModeFreeResources(res);
err_fd:
    close(ctx->fd);
    return -1;
}
// 获取后台 Buffer 的 Frame 信息，以便传给 RGA
int display_get_back_frame(DisplayContext *ctx, Frame *drm_frame);
int display_show(DisplayContext *ctx); 
int display_deinit(DisplayContext *ctx)
{
    if (!ctx || !ctx->is_initialized) return 0;

    if (ctx->saved_crtc) {
        drmModeSetCrtc(ctx->fd, 
                       ctx->saved_crtc->crtc_id, 
                       ctx->saved_crtc->buffer_id,
                       ctx->saved_crtc->x, 
                       ctx->saved_crtc->y, 
                       &ctx->connector_id, 
                       1, 
                       &ctx->saved_crtc->mode);
                       
        drmModeFreeCrtc(ctx->saved_crtc);
        ctx->saved_crtc = NULL;
    }

    // 2. 释放所有的双缓冲/多缓冲资源
    for (int i = 0; i < DRM_BUFFER_COUNT; i++) {
        DrmBuffer *buf = &ctx->buffers[i];

        // 解除用户空间映射
        if (buf->vaddr && buf->vaddr != MAP_FAILED) {
            munmap(buf->vaddr, buf->size);
            buf->vaddr = NULL;
        }
        
        // 关闭导出的 DMA FD
        if (buf->dma_fd > 0) {
            close(buf->dma_fd);
            buf->dma_fd = -1;
        }
        
        // 从 DRM 中移除 Framebuffer
        if (buf->fb_id) {
            drmModeRmFB(ctx->fd, buf->fb_id);
            buf->fb_id = 0;
        }
        
        // 销毁 DRM Dumb Buffer 对象
        if (buf->handle) {
            struct drm_mode_destroy_dumb dreq = { .handle = buf->handle };
            ioctl(ctx->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
            buf->handle = 0;
        }
    }

    if (ctx->fd > 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    ctx->is_initialized = false;
    return 0;
}