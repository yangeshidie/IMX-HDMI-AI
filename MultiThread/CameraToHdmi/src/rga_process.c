#include "rga_process.h"

static int sRgaNv12ToBgraScale(int srcFd, int dstFd,
                                int srcW, int srcH, int dstW, int dstH) {
    rga_info_t src = {0};
    rga_info_t dst = {0};
    
    // 源图信息 (使用 DMA FD)
    src.fd = srcFd;
    src.mmuFlag = 1;
    src.format = RK_FORMAT_YCbCr_420_SP; // NV12
    rga_set_rect(&src.rect, 0, 0, srcW, srcH, srcW, srcH, RK_FORMAT_YCbCr_420_SP);

    // 目标图信息 (使用 DMA FD)
    dst.fd = dstFd;
    dst.mmuFlag = 1;
    dst.format = RK_FORMAT_BGRA_8888;
    rga_set_rect(&dst.rect, 0, 0, dstW, dstH, dstW, dstH, RK_FORMAT_BGRA_8888);

    if (c_RkRgaBlit(&src, &dst, NULL) < 0) {
        fprintf(stderr, "RGA Blit 失败\n");
        return -1;
    }
    return 0;
}