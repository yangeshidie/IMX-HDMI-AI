#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

// 引入 RGA 库
#include <rga/RgaApi.h>
#include <rga/rga.h>

/*************************** 0. 配置宏定义 ***************************/
#define DRM_BPP             32
#define DEV_CAMERA          "/dev/video11"
#define DEV_DRM             "/dev/dri/card0"
#define CAM_WIDTH           3840
#define CAM_HEIGHT          2160
#define CAM_BUFFER_COUNT    4
#define CAM_PIXEL_FMT       V4L2_PIX_FMT_NV12
#define CAM_BUF_TYPE        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE

#define CLIP(x)             ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

/*************************** 1. 结构体定义 ***************************/

typedef struct {
    void *start;
    size_t length;
} buffer_info_t;

typedef struct {
    int cameraFd;
    uint32_t width;
    uint32_t height;
    uint32_t bufferCount;   
    buffer_info_t *buffers;
    int *dma_fds;
    struct v4l2_format fmt;
} camera_device_t;

typedef struct {
    int drmFd;
    uint32_t crtcId;
    uint32_t fbId;
    uint32_t handle;
    int dma_fd;
    uint32_t *pixels;
    uint32_t pitch;
    uint64_t size;
    drmModeModeInfo mode;
    drmModeConnector *conn;
    drmModeCrtc *savedCrtc;  
} drm_device_t;

/*************************** 2. 核心转换函数 ***************************/

/**
 * @brief CPU 版：NV12 转 XRGB8888 + 缩放
 */
static void sCpuNv12ToXrgbScale(uint8_t *srcNv12, uint32_t *dstXrgb, 
                                 int srcW, int srcH, int dstW, int dstH, int stride) {
    uint8_t *yPlane = srcNv12;
    uint8_t *uvPlane = srcNv12 + (stride * srcH);

    for (int y = 0; y < dstH; y++) {
        for (int x = 0; x < dstW; x++) {
            int srcX = x * srcW / dstW;
            int srcY = y * srcH / dstH;

            uint8_t Y = yPlane[srcY * stride + srcX];
            uint8_t U = uvPlane[(srcY / 2) * stride + (srcX / 2) * 2];
            uint8_t V = uvPlane[(srcY / 2) * stride + (srcX / 2) * 2 + 1];

            int C = Y - 16; int D = U - 128; int E = V - 128;
            int R = CLIP((298 * C           + 409 * E + 128) >> 8);
            int G = CLIP((298 * C - 100 * D - 208 * E + 128) >> 8);
            int B = CLIP((298 * C + 516 * D           + 128) >> 8);
            dstXrgb[y * dstW + x] = (R << 16) | (G << 8) | B;
        }
    }
}

/**
 * @brief RGA 版：NV12 转 XRGB8888 + 缩放
 */
static int sRgaNv12ToXrgbScale(int srcFd, int dstFd,
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
    dst.format = RK_FORMAT_XRGB_8888;
    rga_set_rect(&dst.rect, 0, 0, dstW, dstH, dstW, dstH, RK_FORMAT_XRGB_8888);

    if (c_RkRgaBlit(&src, &dst, NULL) < 0) {
        fprintf(stderr, "RGA Blit 失败\n");
        return -1;
    }
    return 0;
}
/*************************** 3. 主程序逻辑 ***************************/

int main() {
    camera_device_t cam = {0};
    drm_device_t drm = {0};
    struct timespec start, end;
    double cpuTime, rgaTime;
    drmModeRes *res;
    drmModeEncoder *enc;
    c_RkRgaInit();
    /************************** 1. 打开设备节点 ****************************/
    cam.cameraFd = open(DEV_CAMERA, O_RDWR);
    drm.drmFd = open(DEV_DRM, O_RDWR | O_CLOEXEC);

    if (cam.cameraFd < 0 || drm.drmFd < 0) {
        perror("设备打开失败");
        return -1;
    }

    /************************** 2. 配置摄像头格式 ***************************/
    cam.fmt.type = CAM_BUF_TYPE;
    cam.fmt.fmt.pix_mp.width = CAM_WIDTH;
    cam.fmt.fmt.pix_mp.height = CAM_HEIGHT;
    cam.fmt.fmt.pix_mp.pixelformat = CAM_PIXEL_FMT;
    cam.fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    cam.fmt.fmt.pix_mp.num_planes = 1; 

    if (ioctl(cam.cameraFd, VIDIOC_S_FMT, &cam.fmt) < 0) {
        perror("V4L2 格式设置失败");
        return -1;
    }

    /************************** 3. 申请摄像头缓冲区 *************************/
    struct v4l2_requestbuffers rqp = {0};
    rqp.count = CAM_BUFFER_COUNT;
    rqp.type = CAM_BUF_TYPE;
    rqp.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam.cameraFd, VIDIOC_REQBUFS, &rqp) < 0) {
        perror("V4L2 申请缓冲区失败");
        return -1;
    }

    cam.bufferCount = rqp.count;
    cam.buffers = calloc(cam.bufferCount, sizeof(buffer_info_t));
    cam.dma_fds = calloc(cam.bufferCount, sizeof(int));

    for (uint32_t i = 0; i < cam.bufferCount; i++) {
        struct v4l2_buffer buf = {0};
        struct v4l2_plane planes[1] = {0};
        buf.type = CAM_BUF_TYPE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = 1;
        buf.m.planes = planes;

        if (ioctl(cam.cameraFd, VIDIOC_QUERYBUF, &buf) < 0) break;

        cam.buffers[i].length = planes[0].length;
        cam.buffers[i].start = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE, 
                                     MAP_SHARED, cam.cameraFd, planes[0].m.mem_offset);
        
        // 导出 DMA FD
        struct v4l2_exportbuffer expbuf = {0};
        expbuf.type = CAM_BUF_TYPE;
        expbuf.index = i;
        if (ioctl(cam.cameraFd, VIDIOC_EXPBUF, &expbuf) == 0) {
            cam.dma_fds[i] = expbuf.fd;
        } else {
            perror("VIDIOC_EXPBUF 失败");
        }
        
        // 预入队空缓冲区 (只保留这一个)
        ioctl(cam.cameraFd, VIDIOC_QBUF, &buf);
    }


    /************************** 4. 配置 DRM 显示器 **************************/
    res = drmModeGetResources(drm.drmFd);
    for (int i = 0; i < res->count_connectors; i++) {
        drm.conn = drmModeGetConnector(drm.drmFd, res->connectors[i]);
        if (drm.conn && drm.conn->connection == DRM_MODE_CONNECTED) break;
        drmModeFreeConnector(drm.conn);
    }

    drm.mode = drm.conn->modes[0];
    enc = drmModeGetEncoder(drm.drmFd, drm.conn->encoder_id);
    drm.crtcId = enc->crtc_id;
    drm.savedCrtc = drmModeGetCrtc(drm.drmFd, drm.crtcId);

    /************************** 5. 创建 DRM Dumb Buffer *********************/
    struct drm_mode_create_dumb creq = {0};
    creq.width = drm.mode.hdisplay;
    creq.height = drm.mode.vdisplay;
    creq.bpp = DRM_BPP;

    ioctl(drm.drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    drm.handle = creq.handle;
    drm.pitch = creq.pitch;
    drm.size = creq.size;

    drmModeAddFB(drm.drmFd, drm.mode.hdisplay, drm.mode.vdisplay, 24, 32, 
                 drm.pitch, drm.handle, &drm.fbId);

    struct drm_mode_map_dumb mreq = {0};
    mreq.handle = drm.handle;
    ioctl(drm.drmFd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);

    drm.pixels = mmap(0, drm.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm.drmFd, mreq.offset);

    // 切换屏幕显示内容到新 FB
    drmModeSetCrtc(drm.drmFd, drm.crtcId, drm.fbId, 0, 0, &drm.conn->connector_id, 1, &drm.mode);
    if (drmPrimeHandleToFD(drm.drmFd, drm.handle, 0, &drm.dma_fd) < 0) {
            perror("DRM Buffer 导出 FD 失败");
        }
    /************************** 6. 开启采集流 ****************************/
    enum v4l2_buf_type type = CAM_BUF_TYPE;
    ioctl(cam.cameraFd, VIDIOC_STREAMON, &type);

    struct v4l2_buffer vBuf = {0};
    struct v4l2_plane planes[1] = {0};
    vBuf.type = CAM_BUF_TYPE;
    vBuf.memory = V4L2_MEMORY_MMAP;
    vBuf.length = 1;
    vBuf.m.planes = planes;

    if (ioctl(cam.cameraFd, VIDIOC_DQBUF, &vBuf) == 0) {
        int stride = cam.fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
        uint8_t *camPtr = (uint8_t *)cam.buffers[vBuf.index].start;
        uint8_t *drmPtr = (uint8_t *)drm.pixels;

        printf("\n>>> 开始性能对比实验 (4K NV12 -> %dx%d XRGB) <<<\n", drm.mode.hdisplay, drm.mode.vdisplay);

        /************************** 7. 测试 CPU 转换速度 ****************************/
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        sCpuNv12ToXrgbScale(camPtr, (uint32_t *)drmPtr, 
                             CAM_WIDTH, CAM_HEIGHT, 
                             drm.mode.hdisplay, drm.mode.vdisplay, stride);
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        cpuTime = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
        printf(" [CPU] 转换耗时: %.3f ms\n", cpuTime);

        // 清屏，防止视觉干扰
        memset(drmPtr, 0, drm.size);
        sleep(1);

        /************************** 8. 测试 RGA 转换速度 ****************************/
        clock_gettime(CLOCK_MONOTONIC, &start);
    
        // 传入 V4L2 的当前帧 FD 和 DRM 的显存 FD
        sRgaNv12ToXrgbScale(cam.dma_fds[vBuf.index], drm.dma_fd, 
                            CAM_WIDTH, CAM_HEIGHT, 
                            drm.mode.hdisplay, drm.mode.vdisplay);
    
        clock_gettime(CLOCK_MONOTONIC, &end);
        rgaTime = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
        printf(" [RGA] 转换耗时: %.3f ms\n", rgaTime);

        /************************** 9. 结果分析 ****************************/
        printf("\n>>> 结论: RGA 速度是 CPU 的 %.2f 倍 <<<\n", cpuTime / rgaTime);
        printf("按回车键退出...\n");
        getchar();
        
        ioctl(cam.cameraFd, VIDIOC_QBUF, &vBuf);
    }

    /************************** 10. 恢复环境与资源释放 ***********************/
    // 停止流
    ioctl(cam.cameraFd, VIDIOC_STREAMOFF, &type);

    // 还原 CRTC
    if (drm.savedCrtc) {
        drmModeSetCrtc(drm.drmFd, drm.savedCrtc->crtc_id, drm.savedCrtc->buffer_id,
                       drm.savedCrtc->x, drm.savedCrtc->y, &drm.conn->connector_id, 1, &drm.savedCrtc->mode);
        drmModeFreeCrtc(drm.savedCrtc);
    }

    // 释放摄像头内存映射 & 关闭 DMA FDs
    for (uint32_t i = 0; i < cam.bufferCount; i++) {
        munmap(cam.buffers[i].start, cam.buffers[i].length);
        if (cam.dma_fds[i] > 0) close(cam.dma_fds[i]); // 新增：关闭 V4L2 的 FD
    }
    free(cam.buffers);
    free(cam.dma_fds); // 新增：释放数组内存

    // 关闭 DRM Dumb Buffer 的 FD
    if (drm.dma_fd > 0) close(drm.dma_fd); // 新增

    // 释放 DRM 显存
    munmap(drm.pixels, drm.size);
    struct drm_mode_destroy_dumb dreq = { .handle = drm.handle };
    ioctl(drm.drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    
    drmModeFreeConnector(drm.conn);
    drmModeFreeResources(res);
    close(cam.cameraFd);
    close(drm.drmFd);

    return 0;
}