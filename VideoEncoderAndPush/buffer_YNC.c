#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/videodev2.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

// 引入 RGA 库
#include <rga/RgaApi.h>
#include <rga/rga.h>

/*************************** 0. 配置宏定义 ***************************/
#define TEST_TIME_LIMIT     10.0   // 极限压测时长：10秒
#define TEST_TIME_STABILITY 900.0  // 稳定流测试时长：15分钟 (900秒)

#define DRM_BPP             32
#define DEV_CAMERA          "/dev/video11"
#define DEV_DRM             "/dev/dri/card0"
#define CAM_WIDTH           3840
#define CAM_HEIGHT          2160
#define CAM_BUFFER_COUNT    4
#define DRM_BUFFER_COUNT    2      // 核心修改：DRM使用双缓冲

#define CAM_PIXEL_FMT       V4L2_PIX_FMT_NV12
#define CAM_BUF_TYPE        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE

/************************* 1. 枚举结构体定义 ***************************/

typedef enum {
    TEST_MODE_STABILITY = 0,  // 0: 15分钟稳定流测试 (双缓冲+VSYNC，无撕裂)
    TEST_MODE_PERFORMANCE = 1 // 1: 10秒极限算力测试 (单缓冲，解锁帧率)
} test_mode_t;

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
    
    // 将单一缓冲区修改为数组，以支持双缓冲
    uint32_t fbId[DRM_BUFFER_COUNT];
    uint32_t handle[DRM_BUFFER_COUNT];
    int dma_fd[DRM_BUFFER_COUNT];
    uint32_t *pixels[DRM_BUFFER_COUNT];
    uint32_t pitch[DRM_BUFFER_COUNT];
    uint64_t size[DRM_BUFFER_COUNT];
    
    drmModeModeInfo mode;
    drmModeConnector *conn;
    drmModeCrtc *savedCrtc;  
} drm_device_t;

/*************************** 2. 核心转换与回调函数 ***************************/

/**
 * @brief RGA 版：NV12 转 XRGB8888/BGRA8888 + 缩放
 */
static int sRgaNv12ToBgraScale(int srcFd, int dstFd,
                                int srcW, int srcH, int dstW, int dstH) {
    rga_info_t src = {0};
    rga_info_t dst = {0};
    
    // 源图信息
    src.fd = srcFd;
    src.mmuFlag = 1;
    src.format = RK_FORMAT_YCbCr_420_SP; // NV12
    rga_set_rect(&src.rect, 0, 0, srcW, srcH, srcW, srcH, RK_FORMAT_YCbCr_420_SP);

    // 目标图信息
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

/**
 * @brief 获取时间差
 */
static double sGetTimeDiff(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

/**
 * @brief DRM Page Flip 的回调函数（用于等待 VSYNC）
 */
static void sPageFlipHandler(int fd, unsigned int frame,
                             unsigned int sec, unsigned int usec, void *data) {
    int *waiting_for_flip = (int *)data;
    *waiting_for_flip = 0; // 收到 VSYNC 信号，将等待标志位置为 0
}

/*************************** 3. 测试功能函数 ***************************/

/**
 * @brief 模式0：15 分钟稳定视频流 (双缓冲 + VSYNC 无撕裂)
 */
static void sRunStabilityTest(camera_device_t *cam, drm_device_t *drm) {
    struct timespec test_start, current_time, last_sec_time;
    int frame_count = 0;       
    int total_frames = 0;      

    // DRM 事件上下文，用于处理 Page Flip
    drmEventContext evCtx = {0};
    evCtx.version = DRM_EVENT_CONTEXT_VERSION;
    evCtx.page_flip_handler = sPageFlipHandler;

    int front_buf = 0; // 当前显示在屏幕上的 Buffer
    int back_buf = 1;  // 后台正在渲染的 Buffer (不可见)

    printf("\n======================================================\n");
    printf(">>> 模式 0: 开始 15 分钟稳定流测试 [双缓冲 + VSYNC] <<<\n");
    printf(">> 预期现象：画面绝对无撕裂，帧率被锁定在屏幕刷新率 (如 60FPS) <<\n");
    printf("======================================================\n");

    clock_gettime(CLOCK_MONOTONIC, &test_start);
    last_sec_time = test_start;

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed_time = sGetTimeDiff(&test_start, &current_time);
        if (elapsed_time >= TEST_TIME_STABILITY) break; 

        struct v4l2_buffer vBuf = {0};
        struct v4l2_plane planes[1] = {0};
        vBuf.type = CAM_BUF_TYPE;
        vBuf.memory = V4L2_MEMORY_MMAP;
        vBuf.length = 1;
        vBuf.m.planes = planes;

        // 1. 获取摄像头帧
        if (ioctl(cam->cameraFd, VIDIOC_DQBUF, &vBuf) == 0) {
            
            // 2. RGA 渲染到 Back Buffer（后台不可见层）
            sRgaNv12ToBgraScale(cam->dma_fds[vBuf.index], drm->dma_fd[back_buf], 
                                CAM_WIDTH, CAM_HEIGHT, 
                                drm->mode.hdisplay, drm->mode.vdisplay);

            // 3. 提交 Page Flip 请求，等待垂直同步
            int waiting_for_flip = 1;
            if (drmModePageFlip(drm->drmFd, drm->crtcId, drm->fbId[back_buf], 
                                DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip) == 0) {
                
                // 使用 select 阻塞等待 DRM 的 VSYNC 事件
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(drm->drmFd, &fds);
                
                while (waiting_for_flip) {
                    select(drm->drmFd + 1, &fds, NULL, NULL, NULL);
                    drmHandleEvent(drm->drmFd, &evCtx);
                }
            }

            // 4. 角色互换：刚才的 Back 变成了 Front，刚才的 Front 变成了下一个 Back
            front_buf = back_buf;
            back_buf ^= 1; 

            // 5. 帧率统计
            frame_count++;
            total_frames++;
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            if (sGetTimeDiff(&last_sec_time, &current_time) >= 1.0) {
                int min = (int)elapsed_time / 60;
                int sec = (int)elapsed_time % 60;
                printf("[VSYNC 稳定测试] 进度: %02d:%02d / 15:00 | 锁定帧率: %d FPS\n", min, sec, frame_count);
                frame_count = 0;
                last_sec_time = current_time;
            }

            // 6. 缓冲区交还摄像头
            ioctl(cam->cameraFd, VIDIOC_QBUF, &vBuf);
        }
    }
    printf("\n--- 15分钟测试完成！共渲染帧数: %d 帧 ---\n", total_frames);
}

/**
 * @brief 模式1：10 秒极限吞吐量测试 (单缓冲，解锁 FPS)
 */
static void sRunPerformanceTest(camera_device_t *cam, drm_device_t *drm) {
    struct timespec test_start, current_time;
    int total_frames = 0; 

    printf("\n======================================================\n");
    printf(">>> 模式 1: 开始 10 秒极限算力测试 [单缓冲解锁帧率] <<<\n");
    printf(">> 预期现象：画面可能撕裂闪烁，但在测试 RGA 芯片的真实极限吞吐量 <<\n");
    printf("======================================================\n");

    struct v4l2_buffer vBuf = {0};
    struct v4l2_plane planes[1] = {0};
    vBuf.type = CAM_BUF_TYPE;
    vBuf.memory = V4L2_MEMORY_MMAP;
    vBuf.length = 1;
    vBuf.m.planes = planes;

    // 从摄像头只取出 1 帧用于反复死命渲染
    if (ioctl(cam->cameraFd, VIDIOC_DQBUF, &vBuf) < 0) {
        perror("压测前抓取画面失败"); return;
    }

    int current_dma_fd = cam->dma_fds[vBuf.index];
    clock_gettime(CLOCK_MONOTONIC, &test_start);

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed_time = sGetTimeDiff(&test_start, &current_time);
        if (elapsed_time >= TEST_TIME_LIMIT) break; 

        // RGA 疯狂复写同一个显存 (会撕裂，但是能测出极限速度)
        sRgaNv12ToBgraScale(current_dma_fd, drm->dma_fd[0], 
                            CAM_WIDTH, CAM_HEIGHT, 
                            drm->mode.hdisplay, drm->mode.vdisplay);
        total_frames++;
    }

    ioctl(cam->cameraFd, VIDIOC_QBUF, &vBuf);

    double max_fps = total_frames / TEST_TIME_LIMIT;
    printf("\n--- 10秒极限压测结束 ---\n");
    printf("爆刷总帧数: %d 帧\n", total_frames);
    printf("RGA 理论极限帧率: %.2f FPS\n", max_fps);
}

/*************************** 4. 主程序逻辑 ***************************/

int main(int argc, char **argv) {
    camera_device_t cam = {0};
    drm_device_t drm = {0};
    drmModeRes *res;
    drmModeEncoder *enc;

    test_mode_t current_mode = TEST_MODE_STABILITY; 
    
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("用法: %s [测试模式]\n", argv[0]);
            printf("  0 : [默认] 15 分钟稳定视频流 (双缓冲/无撕裂/VSYNC锁定)\n");
            printf("  1 : 10 秒极限算力压测 (解锁帧率，测试芯片极限)\n");
            return 0;
        }
        int mode_arg = atoi(argv[1]);
        if (mode_arg == 0 || mode_arg == 1) current_mode = (test_mode_t)mode_arg;
        else { fprintf(stderr, "无效模式\n"); return -1; }
    }

    c_RkRgaInit();
    cam.cameraFd = open(DEV_CAMERA, O_RDWR);
    drm.drmFd = open(DEV_DRM, O_RDWR | O_CLOEXEC);
    if (cam.cameraFd < 0 || drm.drmFd < 0) return -1;

    // --- 初始化 V4L2 ---
    cam.fmt.type = CAM_BUF_TYPE;
    cam.fmt.fmt.pix_mp.width = CAM_WIDTH;
    cam.fmt.fmt.pix_mp.height = CAM_HEIGHT;
    cam.fmt.fmt.pix_mp.pixelformat = CAM_PIXEL_FMT;
    cam.fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    cam.fmt.fmt.pix_mp.num_planes = 1; 
    ioctl(cam.cameraFd, VIDIOC_S_FMT, &cam.fmt);

    struct v4l2_requestbuffers rqp = {0};
    rqp.count = CAM_BUFFER_COUNT;
    rqp.type = CAM_BUF_TYPE;
    rqp.memory = V4L2_MEMORY_MMAP;
    ioctl(cam.cameraFd, VIDIOC_REQBUFS, &rqp);

    cam.bufferCount = rqp.count;
    cam.buffers = calloc(cam.bufferCount, sizeof(buffer_info_t));
    cam.dma_fds = calloc(cam.bufferCount, sizeof(int));

    for (uint32_t i = 0; i < cam.bufferCount; i++) {
        struct v4l2_buffer buf = { .type = CAM_BUF_TYPE, .memory = V4L2_MEMORY_MMAP, .index = i, .length = 1 };
        struct v4l2_plane planes[1] = {0}; buf.m.planes = planes;
        ioctl(cam.cameraFd, VIDIOC_QUERYBUF, &buf);

        cam.buffers[i].length = planes[0].length;
        cam.buffers[i].start = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, cam.cameraFd, planes[0].m.mem_offset);
        
        struct v4l2_exportbuffer expbuf = { .type = CAM_BUF_TYPE, .index = i };
        if (ioctl(cam.cameraFd, VIDIOC_EXPBUF, &expbuf) == 0) cam.dma_fds[i] = expbuf.fd;
        
        ioctl(cam.cameraFd, VIDIOC_QBUF, &buf);
    }

    // --- 初始化 DRM 与 双缓冲 ---
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

    // 【核心变化】：申请两个显示缓冲区
    for(int i = 0; i < DRM_BUFFER_COUNT; i++) {
        struct drm_mode_create_dumb creq = {0};
        creq.width = drm.mode.hdisplay;
        creq.height = drm.mode.vdisplay;
        creq.bpp = DRM_BPP;
        ioctl(drm.drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
        drm.handle[i] = creq.handle;
        drm.pitch[i] = creq.pitch;
        drm.size[i] = creq.size;

        drmModeAddFB(drm.drmFd, drm.mode.hdisplay, drm.mode.vdisplay, 24, 32, drm.pitch[i], drm.handle[i], &drm.fbId[i]);
        struct drm_mode_map_dumb mreq = { .handle = drm.handle[i] };
        ioctl(drm.drmFd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
        drm.pixels[i] = mmap(0, drm.size[i], PROT_READ | PROT_WRITE, MAP_SHARED, drm.drmFd, mreq.offset);
        drmPrimeHandleToFD(drm.drmFd, drm.handle[i], 0, &drm.dma_fd[i]);
        
        memset(drm.pixels[i], 0, drm.size[i]); // 清黑屏
    }

    // 默认先将屏幕指向 fbId[0]
    drmModeSetCrtc(drm.drmFd, drm.crtcId, drm.fbId[0], 0, 0, &drm.conn->connector_id, 1, &drm.mode);

    // --- 启动并测试 ---
    enum v4l2_buf_type type = CAM_BUF_TYPE;
    ioctl(cam.cameraFd, VIDIOC_STREAMON, &type);

    if (current_mode == TEST_MODE_STABILITY) {
        sRunStabilityTest(&cam, &drm);
    } else {
        sRunPerformanceTest(&cam, &drm);
    }

    // --- 资源释放 ---
    ioctl(cam.cameraFd, VIDIOC_STREAMOFF, &type);
    if (drm.savedCrtc) {
        drmModeSetCrtc(drm.drmFd, drm.savedCrtc->crtc_id, drm.savedCrtc->buffer_id,
                       drm.savedCrtc->x, drm.savedCrtc->y, &drm.conn->connector_id, 1, &drm.savedCrtc->mode);
        drmModeFreeCrtc(drm.savedCrtc);
    }

    for (uint32_t i = 0; i < cam.bufferCount; i++) {
        munmap(cam.buffers[i].start, cam.buffers[i].length);
        if (cam.dma_fds[i] > 0) close(cam.dma_fds[i]);
    }
    free(cam.buffers); free(cam.dma_fds);

    for (int i = 0; i < DRM_BUFFER_COUNT; i++) {
        if (drm.dma_fd[i] > 0) close(drm.dma_fd[i]);
        munmap(drm.pixels[i], drm.size[i]);
        struct drm_mode_destroy_dumb dreq = { .handle = drm.handle[i] };
        ioctl(drm.drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    }
    
    drmModeFreeConnector(drm.conn);
    drmModeFreeResources(res);
    close(cam.cameraFd); close(drm.drmFd);

    return 0;
}