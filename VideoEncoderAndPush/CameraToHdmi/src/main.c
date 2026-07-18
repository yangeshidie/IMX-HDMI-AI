#include "../include/camera.h"
#include "../include/display.h"
#include "../include/rga_process.h"
#include "../include/frame_queue.h"
#include "../include/common.h"
#include "../include/mpp_encoder.h"

#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>

#define DEV_CAMERA                  "/dev/video11"
#define DEV_DRM                     "/dev/dri/card0"
#define VERSION                     "1.1：MPP对齐官方\n"

#define CAM_WIDTH                   3840
#define CAM_HEIGHT                  2160
#define CAM_BUFFER_COUNT            4
#define CAM_PIXEL_FMT               V4L2_PIX_FMT_NV12
#define CAM_BUF_TYPE                V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE

#define SIGNAL_CONTINUE             0
#define SIGNAL_QUIT                 1

#define CLIP(x)                     ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

#define MPP_ENCODER_FPS             30

#define QUEUE_LENTH_DISPLAY         4
#define QUEUE_LENTH_ENCODE          4
#define QUEUE_LENTH_AI              1



pthread_mutex_t quitMutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t g_quit = SIGNAL_CONTINUE;

typedef struct 
{
    DisplayContext  display;     // 显示硬件上下文
    CameraContext   camera;      // 摄像头硬件上下文
    EncoderContext  encoder;

    FrameQueue      display_q;   // 显示线程循环队列
    FrameQueue      encode_q;    // 编码线程循环队列
    FrameQueue      ai_q;        // AI线程循环队列

} appContext;


int system_init(appContext *ctx, const char *dev_name_camera, const char *dev_name_display, uint32_t w, uint32_t h, 
                uint32_t count, uint32_t pixel_format, uint32_t buf_type, uint32_t mpp_encoder_fps)
{
    if (camera_init(&ctx->camera, dev_name_camera, w, h, count, pixel_format, buf_type) < 0) return -1;
    if (display_init(&ctx->display, dev_name_display) < 0) return -1;
    if (mpp_encoder_init(&ctx->encoder, w, h, mpp_encoder_fps) < 0) return -1;
    if (rga_process_init() < 0) return -1;

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
    rga_process_deinit();
    mpp_encoder_deinit(&ctx->encoder);

    frame_queue_deinit(&ctx->display_q);
    frame_queue_deinit(&ctx->encode_q );
    frame_queue_deinit(&ctx->ai_q     );
    return 0;
}

void handle_sigint(int sig) {
    g_quit = SIGNAL_QUIT; 
}

void* cameraThread(void *arg) 
{
    appContext *ctx = (appContext *)arg;
    if (camera_start(&ctx->camera) < 0) {
        fprintf(stderr, "Camera 线程启动失败\n");
        return NULL;
    }
    printf("[Camera] 线程已启动...\n");
    while (g_quit == SIGNAL_CONTINUE) {
        Frame frame;
        if (camera_get_frame(&ctx->camera, &frame) < 0) {
            if (errno == EINTR || g_quit == SIGNAL_QUIT) break;
            usleep(10000); 
            continue;
        }

#if THREAD_DISPLAY
        frame_retain(&frame);
        if (frame_queue_push(&ctx->display_q, &frame) < 0) {
            frame_release(&frame);
        }
#endif

#if THREAD_ENCODER
        frame_retain(&frame);
        if (frame_queue_push(&ctx->encode_q, &frame) < 0) {
            frame_release(&frame);
        }
#endif

#if THREAD_AI
        frame_retain(&frame);
        if (frame_queue_push(&ctx->ai_q, &frame) < 0) {
            frame_release(&frame);
        }
#endif
        frame_release(&frame); 
    }

    printf("[Camera] 线程安全退出。\n");
    return NULL;
}

void* displayThread(void *arg) 
{
    appContext *ctx = (appContext *)arg;
    printf("[Display] 线程已启动...\n");
    while (g_quit == SIGNAL_CONTINUE) {
        Frame cam_frame;
        if (frame_queue_pop(&ctx->display_q, &cam_frame) < 0) {
            break; 
        }
        Frame drm_frame;
        if (display_get_back_frame(&ctx->display, &drm_frame) == 0) {
            if (rga_process_convert_scale(&cam_frame, &drm_frame) == 0) {
                frame_release(&cam_frame); 
                display_show(&ctx->display);
            } else {
                frame_release(&cam_frame); 
            }
        } else {
            frame_release(&cam_frame); 
        }
    }
    printf("[Display] 线程安全退出。\n");
    return NULL;
}

void* encoderThread(void *arg) 
{
    appContext *ctx = (appContext *)arg;
    printf("[Encoder] 线程已启动...\n");
    FILE *fp = fopen("record_output.h264", "wb");
    if (!fp) {
        perror("[Encoder] 创建视频文件失败");
    }
    if (fp && ctx->encoder.header_data && ctx->encoder.header_size > 0) {
        fwrite(ctx->encoder.header_data, 1, ctx->encoder.header_size, fp);
        fflush(fp); 
    }

    while (g_quit == SIGNAL_CONTINUE) {
        Frame cam_frame;
        if (frame_queue_pop(&ctx->encode_q, &cam_frame) < 0) {
            break; 
        }
        EncodedPacket out_packet = {0};
        if (mpp_encoder_encode(&ctx->encoder, &cam_frame, &out_packet) == 0) {
            if (fp) {
                fwrite(out_packet.data, 1, out_packet.length, fp);
            }
            mpp_encoder_release_packet(&out_packet);
        }

        frame_release(&cam_frame);
    }

    if (fp) {
        fclose(fp);
    }
    printf("[Encoder] 线程安全退出。\n");
    return NULL;
}

int main()
{
    printf(VERSION);
    signal(SIGINT, handle_sigint);
    
    appContext ctx;
    if (system_init(&ctx, DEV_CAMERA, DEV_DRM, CAM_WIDTH, CAM_HEIGHT, 
                    CAM_BUFFER_COUNT, CAM_PIXEL_FMT, CAM_BUF_TYPE, MPP_ENCODER_FPS) < 0) {
        return -1;
    }
    pthread_t tid_camera;
    pthread_create(&tid_camera, NULL, cameraThread, &ctx);

#if THREAD_DISPLAY
    pthread_t tid_display;
    pthread_create(&tid_display, NULL, displayThread, &ctx);
#endif

#if THREAD_ENCODER
    pthread_t tid_encoder;
    pthread_create(&tid_encoder, NULL, encoderThread, &ctx);
#endif

    while(g_quit == SIGNAL_CONTINUE) {
        pause(); 
    }

#if THREAD_DISPLAY
    frame_queue_abort(&ctx.display_q);
    pthread_join(tid_display, NULL);
#endif

#if THREAD_ENCODER
    frame_queue_abort(&ctx.encode_q);
    pthread_join(tid_encoder, NULL);
#endif

#if THREAD_AI
    frame_queue_abort(&ctx.ai_q);
    pthread_join(tid_ai, NULL);
#endif
    camera_stop(&ctx.camera);
    pthread_join(tid_camera, NULL);
    system_deinit(&ctx);
    printf("系统资源清理完毕，程序安全退出。\n");
    return 0;
}