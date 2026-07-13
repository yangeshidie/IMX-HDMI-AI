#include "../include/rga_process.h"

int rga_process_init()
{
    c_RkRgaInit();
    return 0;
}

int rga_process_convert_scale(const Frame *src_frame, const Frame *dst_frame)
{
    rga_info_t src = {0};
    rga_info_t dst = {0};
    
    // 源图信息 (使用 DMA FD)
    src.fd = src_frame->dma_fd;
    src.mmuFlag = 1;
    src.format = RK_FORMAT_YCbCr_420_SP; // NV12
    rga_set_rect(&src.rect, 0, 0, src_frame->width, src_frame->height,  src_frame->width, src_frame->height, RK_FORMAT_YCbCr_420_SP);

    // 目标图信息 (使用 DMA FD)
    dst.fd = dst_frame->dma_fd;
    dst.mmuFlag = 1;
    dst.format = RK_FORMAT_BGRA_8888;
    rga_set_rect(&dst.rect, 0, 0, dst_frame->width, dst_frame->height,  dst_frame->width, dst_frame->height, RK_FORMAT_BGRA_8888);

    if (c_RkRgaBlit(&src, &dst, NULL) < 0) {
        fprintf(stderr, "RGA Blit 失败\n");
        return -1;
    }
    return 0;
}