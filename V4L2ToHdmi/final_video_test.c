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
#define TEST_TIME           15.0
#define TEST_STREAM_TIME    900.0
#define DRM_BPP             32
#define DEV_CAMERA          "/dev/video11"
#define DEV_DRM             "/dev/dri/card0"
#define CAM_WIDTH           3840
#define CAM_HEIGHT          2160
#define CAM_BUFFER_COUNT    4
#define CAM_PIXEL_FMT       V4L2_PIX_FMT_NV12
#define CAM_BUF_TYPE        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE

#define CLIP(x)             ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

#define TEST_MODE_FRAME_RATE
#define TEST_MODE_           
/************************* 1. 枚举结构体定义 ***************************/

typedef enum {
    TEST_MODE_STREAMING = 0,  // 0: 仅视频流实时采集测试
    TEST_MODE_LIMIT     = 1,  // 1: 仅极限吞吐量压测
    TEST_MODE_BOTH      = 2,  // 2: 运行全部测试
    TEST_MODE_STREAM    = 3   // 3: 15分钟RGA连续视频流测试
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

/**
 * @brief 获取两个时间点之间的秒数差值
 */
static double sGetTimeDiff(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

/**
 * @brief 流媒体性能测试函数
 * @param use_rga: 1 表示使用 RGA，0 表示使用 CPU
 */
static void sRunStreamingTest(camera_device_t *cam, drm_device_t *drm, int use_rga) {
    struct timespec test_start, current_time, last_sec_time;
    struct timespec process_start, process_end;
    
    int frame_count = 0;       // 1秒内的帧数
    int total_frames = 0;      // 15秒总帧数
    double total_process_time = 0.0; // 累计处理耗时（用于算平均延迟）

    printf("\n======================================================\n");
    printf(">>> 开始 15 秒连续视频流测试 - 模式: [%s] <<<\n", use_rga ? "RGA 硬件加速" : "CPU 纯计算");
    printf("======================================================\n");

    clock_gettime(CLOCK_MONOTONIC, &test_start);
    last_sec_time = test_start;

    // 循环条件：测试总时间未达到 TEST_TIME 秒
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        if (sGetTimeDiff(&test_start, &current_time) >= TEST_TIME) {
            break; 
        }

        struct v4l2_buffer vBuf = {0};
        struct v4l2_plane planes[1] = {0};
        vBuf.type = CAM_BUF_TYPE;
        vBuf.memory = V4L2_MEMORY_MMAP;
        vBuf.length = 1;
        vBuf.m.planes = planes;

        // 1. 从摄像头取出一帧
        if (ioctl(cam->cameraFd, VIDIOC_DQBUF, &vBuf) == 0) {
            
            // --- 记录处理开始时间 ---
            clock_gettime(CLOCK_MONOTONIC, &process_start);

            if (use_rga) {
                // RGA 转换
                sRgaNv12ToBgraScale(cam->dma_fds[vBuf.index], drm->dma_fd, 
                                    CAM_WIDTH, CAM_HEIGHT, 
                                    drm->mode.hdisplay, drm->mode.vdisplay);
            } else {
                // CPU 转换
                int stride = cam->fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
                uint8_t *camPtr = (uint8_t *)cam->buffers[vBuf.index].start;
                uint8_t *drmPtr = (uint8_t *)drm->pixels;
                sCpuNv12ToXrgbScale(camPtr, (uint32_t *)drmPtr, 
                                    CAM_WIDTH, CAM_HEIGHT, 
                                    drm->mode.hdisplay, drm->mode.vdisplay, stride);
            }

            // --- 记录处理结束时间，并累加耗时 ---
            clock_gettime(CLOCK_MONOTONIC, &process_end);
            total_process_time += sGetTimeDiff(&process_start, &process_end);

            frame_count++;
            total_frames++;

            // 2. 每隔 1 秒输出一次 FPS
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            if (sGetTimeDiff(&last_sec_time, &current_time) >= 1.0) {
                printf("[%s] 当前输出帧率: %d FPS\n", use_rga ? "RGA" : "CPU", frame_count);
                frame_count = 0; // 重置1秒计数器
                last_sec_time = current_time;
            }

            // 3. 将空篮子还给摄像头
            ioctl(cam->cameraFd, VIDIOC_QBUF, &vBuf);
        }
    }

    // 测试结束，输出汇总报告
    printf("\n--- [%s] 15秒测试完成 ---\n", use_rga ? "RGA" : "CPU");
    printf("总渲染帧数: %d 帧\n", total_frames);
    printf("平均流帧率: %.2f FPS\n", total_frames / 15.0);
    printf("平均单帧转换耗时: %.2f ms\n", (total_process_time / total_frames) * 1000.0);
}

/**
 * @brief 极限吞吐量压测函数
 * @param use_rga: 1 表示使用 RGA，0 表示使用 CPU
 */
static void sRunLimitBenchmark(camera_device_t *cam, drm_device_t *drm, int use_rga) {
    struct timespec test_start, current_time;
    int total_frames = 0;      // 15秒总处理帧数

    printf("\n======================================================\n");
    printf(">>> 开始 15 秒【极限帧率】压测 - 模式: [%s] <<<\n", use_rga ? "RGA 硬件加速" : "CPU 纯计算");
    printf("======================================================\n");

    struct v4l2_buffer vBuf = {0};
    struct v4l2_plane planes[1] = {0};
    vBuf.type = CAM_BUF_TYPE;
    vBuf.memory = V4L2_MEMORY_MMAP;
    vBuf.length = 1;
    vBuf.m.planes = planes;

    // 1. 从摄像头【只取出一帧】真实画面作为源数据
    if (ioctl(cam->cameraFd, VIDIOC_DQBUF, &vBuf) < 0) {
        perror("压测前抓取画面失败");
        return;
    }

    int stride = cam->fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    uint8_t *camPtr = (uint8_t *)cam->buffers[vBuf.index].start;
    uint8_t *drmPtr = (uint8_t *)drm->pixels;
    int current_dma_fd = cam->dma_fds[vBuf.index];

    // 开始计时
    clock_gettime(CLOCK_MONOTONIC, &test_start);

    // 2. 进入死循环，疯狂处理这一帧！
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        if (sGetTimeDiff(&test_start, &current_time) >= TEST_TIME) {
            break; // 跑满 15 秒退出
        }

        if (use_rga) {
            // RGA 疯狂搬运
            sRgaNv12ToBgraScale(current_dma_fd, drm->dma_fd, 
                                CAM_WIDTH, CAM_HEIGHT, 
                                drm->mode.hdisplay, drm->mode.vdisplay);
        } else {
            // CPU 疯狂计算
            sCpuNv12ToXrgbScale(camPtr, (uint32_t *)drmPtr, 
                                CAM_WIDTH, CAM_HEIGHT, 
                                drm->mode.hdisplay, drm->mode.vdisplay, stride);
        }

        total_frames++;
    }

    // 3. 测试结束，把一直霸占的这帧画面还回去
    ioctl(cam->cameraFd, VIDIOC_QBUF, &vBuf);

    // 4. 输出极限测试报告
    double actual_time = sGetTimeDiff(&test_start, &current_time);
    double max_fps = total_frames / actual_time;
    double min_latency = (actual_time / total_frames) * 1000.0;

    printf("\n--- [%s] 极限压测报告 ---\n", use_rga ? "RGA" : "CPU");
    printf("15秒内爆刷总帧数: %d 帧\n", total_frames);
    printf("理论极限帧率: %.2f FPS  <--- 这是真实算力\n", max_fps);
    printf("理论最小延迟: %.2f ms\n", min_latency);
}
/**
 * @brief 15分钟连续视频流稳定性测试函数 (仅限 RGA)
 */
static void sRunLongTimeStreamingTest(camera_device_t *cam, drm_device_t *drm) {
    struct timespec test_start, current_time, last_sec_time;
    struct timespec process_start, process_end;
    
    int frame_count = 0;       // 1秒内的帧数
    int total_frames = 0;      // 总帧数
    double total_process_time = 0.0; // 累计处理耗时

    printf("\n======================================================\n");
    printf(">>> 开始 15 分钟连续视频流测试 - 模式: [RGA 硬件加速] <<<\n");
    printf("======================================================\n");

    clock_gettime(CLOCK_MONOTONIC, &test_start);
    last_sec_time = test_start;

    // 循环条件：测试总时间未达到 TEST_STREAM_TIME 秒
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed_time = sGetTimeDiff(&test_start, &current_time);
        if (elapsed_time >= TEST_STREAM_TIME) {
            break; 
        }

        struct v4l2_buffer vBuf = {0};
        struct v4l2_plane planes[1] = {0};
        vBuf.type = CAM_BUF_TYPE;
        vBuf.memory = V4L2_MEMORY_MMAP;
        vBuf.length = 1;
        vBuf.m.planes = planes;

        // 1. 从摄像头取出一帧
        if (ioctl(cam->cameraFd, VIDIOC_DQBUF, &vBuf) == 0) {
            
            // --- 记录处理开始时间 ---
            clock_gettime(CLOCK_MONOTONIC, &process_start);

            // 仅使用 RGA 转换
            sRgaNv12ToBgraScale(cam->dma_fds[vBuf.index], drm->dma_fd, 
                                CAM_WIDTH, CAM_HEIGHT, 
                                drm->mode.hdisplay, drm->mode.vdisplay);

            // --- 记录处理结束时间，并累加耗时 ---
            clock_gettime(CLOCK_MONOTONIC, &process_end);
            total_process_time += sGetTimeDiff(&process_start, &process_end);

            frame_count++;
            total_frames++;

            // 2. 每隔 1 秒输出一次实时 FPS 和 进度
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            if (sGetTimeDiff(&last_sec_time, &current_time) >= 1.0) {
                int minutes = (int)elapsed_time / 60;
                int seconds = (int)elapsed_time % 60;
                printf("[RGA 15Min压测] 进度: %02d:%02d / 15:00, 当前帧率: %d FPS\n", 
                        minutes, seconds, frame_count);
                frame_count = 0; // 重置1秒计数器
                last_sec_time = current_time;
            }

            // 3. 将空篮子还给摄像头
            ioctl(cam->cameraFd, VIDIOC_QBUF, &vBuf);
        }
    }

    // 测试结束，输出汇总报告
    printf("\n--- [RGA] 15分钟测试完成 ---\n");
    printf("总渲染帧数: %d 帧\n", total_frames);
    printf("平均流帧率: %.2f FPS\n", total_frames / TEST_STREAM_TIME);
    printf("平均单帧转换耗时: %.2f ms\n", (total_process_time / total_frames) * 1000.0);
}
/*************************** 3. 主程序逻辑 ***************************/

int main(int argc, char **argv) {
    camera_device_t cam = {0};
    drm_device_t drm = {0};
    drmModeRes *res;
    drmModeEncoder *enc;

    test_mode_t current_mode = TEST_MODE_BOTH; // 默认运行全部测试
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("用法: %s [测试模式]\n", argv[0]);
            printf("可选模式:\n");
            printf("  0 : 仅运行视频流实时采集测试 (Streaming Test)\n");
            printf("  1 : 仅运行极限吞吐量压测 (Limit Benchmark)\n");
            printf("  2 : 运行全部测试 (默认)\n");
            printf("  3 : 运行 15 分钟连续视频流测试 (RGA长测)\n"); 
            return 0;
        }
        
        int mode_arg = atoi(argv[1]);
        // 将这里的 <= 2 改为 <= 3
        if (mode_arg >= 0 && mode_arg <= 3) { 
            current_mode = (test_mode_t)mode_arg;
        } else {
            fprintf(stderr, "无效的测试模式！请使用 -h 查看帮助。\n");
            return -1;
        }
    }

    printf(">>> 当前配置的运行模式: %d <<<\n\n", current_mode);
    // ---------------------------------
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
    if (ioctl(cam.cameraFd, VIDIOC_STREAMON, &type) < 0) {
        perror("开启视频流失败");
        return -1;
    }
    /************************** 7. 运行测试 (Switch 逻辑) ****************************/
    switch (current_mode) {
        case TEST_MODE_STREAMING:
            memset(drm.pixels, 0, drm.size);
            sRunStreamingTest(&cam, &drm, 0); // CPU 视频流
            
            printf("\n等待 3 秒后开始 RGA 视频流测试...\n");
            memset(drm.pixels, 0, drm.size);
            sleep(3);
            sRunStreamingTest(&cam, &drm, 1); // RGA 视频流
            break;

        case TEST_MODE_LIMIT:
            memset(drm.pixels, 0, drm.size);
            sRunLimitBenchmark(&cam, &drm, 0); // CPU 极限帧率
            
            printf("\n等待 3 秒后开始 RGA 极限帧率测试...\n");
            memset(drm.pixels, 0, drm.size);
            sleep(3);
            sRunLimitBenchmark(&cam, &drm, 1); // RGA 极限帧率
            break;

        case TEST_MODE_BOTH:
            // === 阶段一：视频流测试 ===
            memset(drm.pixels, 0, drm.size);
            sRunStreamingTest(&cam, &drm, 0);
            
            printf("\n等待 3 秒后开始 RGA 视频流测试...\n");
            memset(drm.pixels, 0, drm.size);
            sleep(3);
            sRunStreamingTest(&cam, &drm, 1);

            printf("\n=============== 流媒体测试结束，准备进入极限压测 ===============\n");
            sleep(3);

            // === 阶段二：极限压测 ===
            memset(drm.pixels, 0, drm.size);
            sRunLimitBenchmark(&cam, &drm, 0);
            
            printf("\n等待 3 秒后开始 RGA 极限帧率测试...\n");
            memset(drm.pixels, 0, drm.size);
            sleep(3);
            sRunLimitBenchmark(&cam, &drm, 1);
            break;
        case TEST_MODE_STREAM:
            memset(drm.pixels, 0, drm.size);
            sRunLongTimeStreamingTest(&cam, &drm); 
            break;

        default:
            fprintf(stderr, "未知的运行模式\n");
            break;
    }

    /************************** 8. 恢复环境与资源释放 ***********************/
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