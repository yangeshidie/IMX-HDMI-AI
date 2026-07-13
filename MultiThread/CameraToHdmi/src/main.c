#include <pthread.h>
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

pthread_cond_t myCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;
int g_box = 0; 

void* productor(void* arg)
{
    for(int i = 0; i < 10; i++)
    {
        pthread_mutex_lock(&myMutex);
        
        g_box = 1; 
        printf("【生产者】放回了货物 [%d]\n", i + 1);
        
        pthread_cond_signal(&myCond);
        
        pthread_mutex_unlock(&myMutex); 
        
        sleep(1); 
    }
    return NULL;
}

void* consumer(void* arg)
{
    for(int i = 0; i < 10; i++)
    {
        pthread_mutex_lock(&myMutex); 
        
        while(g_box == 0)
        {
            pthread_cond_wait(&myCond, &myMutex);
        }
        
        g_box = 0; 
        printf("【消费者】拿走了货物 [%d]\n", i + 1);
        
        pthread_mutex_unlock(&myMutex); 
    }
    return NULL;
}

int main()
{
    pthread_t Productor;
    pthread_t Consumer;

    pthread_create(&Productor, NULL, productor, NULL);
    pthread_create(&Consumer, NULL, consumer, NULL);

    pthread_join(Productor, NULL);
    pthread_join(Consumer, NULL);

    printf("交易结束。\n");
    return 0;
}






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
} camera_ctx_t;

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
} drm_ctx_t;

/*************************** 2. 核心转换函数 ***************************/
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

/*************************** 3. 主程序逻辑 ***************************/

int main(int argc, char **argv) {
    camera_ctx_t cam = {0};
    drm_ctx_t drm = {0};
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