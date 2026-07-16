#include "../include/camera.h"
#include "../include/display.h"
#include "../include/rga_process.h"
#include "../include/frame_queue.h"
#include "../include/common.h"
#include <sys/time.h>
#include <signal.h>

#define DEV_CAMERA                  "/dev/video11"
#define DEV_DRM                     "/dev/dri/card0"

#define CAM_WIDTH                   3840
#define CAM_HEIGHT                  2160
#define CAM_BUFFER_COUNT            4
#define CAM_PIXEL_FMT               V4L2_PIX_FMT_NV12
#define CAM_BUF_TYPE                V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE

#define SIGNAL_CONTINUE             0
#define SIGNAL_QUIT                 1

#define CLIP(x)                     ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

#define QUEUE_LENTH_DISPLAY         4
#define QUEUE_LENTH_ENCODE          4
#define QUEUE_LENTH_AI              1

pthread_mutex_t quitMutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t g_quit = SIGNAL_CONTINUE;

typedef struct 
{
    DisplayContext  display;     // 显示硬件上下文
    CameraContext   camera;      // 摄像头硬件上下文

    FrameQueue      display_q;   // 显示线程循环队列
    FrameQueue      encode_q;    // 编码线程循环队列
    FrameQueue      ai_q;        // AI线程循环队列

} appContext;


int system_init(appContext *ctx, const char *dev_name_camera, const char *dev_name_display, uint32_t w, uint32_t h, 
                uint32_t count, uint32_t pixel_format, uint32_t buf_type)
{
    if (camera_init(&ctx->camera, dev_name_camera, w, h, count, pixel_format, buf_type) < 0) return -1;
    if (display_init(&ctx->display, dev_name_display) < 0) return -1;

    if (frame_queue_init(&ctx->display_q, QUEUE_LENTH_DISPLAY) < 0) return -1;
    if (frame_queue_init(&ctx->encode_q , QUEUE_LENTH_ENCODE ) < 0) return -1;
    if (frame_queue_init(&ctx->ai_q     , QUEUE_LENTH_AI     ) < 0) return -1;
    return 0;
}

int system_deinit(appContext* ctx)
{
    camera_stop(&ctx->camera);
    camera_deinit(&ctx->camera);
    display_deinit(&ctx->display);

    frame_queue_deinit(&ctx->display_q);
    frame_queue_deinit(&ctx->encode_q );
    frame_queue_deinit(&ctx->ai_q     );
    return 0;
}

void handle_sigint(int sig) {
    g_quit = SIGNAL_QUIT; 
}

int main()
{
    signal(SIGINT, handle_sigint);
    
    appContext ctx;
    if (system_init(&ctx, DEV_CAMERA, DEV_DRM, CAM_WIDTH, CAM_HEIGHT, 
                    CAM_BUFFER_COUNT, CAM_PIXEL_FMT, CAM_BUF_TYPE) < 0) {
        return -1;
    }

    pthread_t tid_camera;
    pthread_t tid_display;

    // pthread_create(&tid_camera, NULL, cameraThread, &ctx);
    // pthread_create(&tid_display, NULL, displayThread, &ctx);

    while(g_quit == SIGNAL_CONTINUE) {
        pause(); 
    }

    // frame_queue_abort(&ctx.queue);
    // pthread_join(tid_camera, NULL);
    // pthread_join(tid_display, NULL);

    system_deinit(&ctx);
    printf("系统资源清理完毕，程序安全退出。\n");
    return 0;
}