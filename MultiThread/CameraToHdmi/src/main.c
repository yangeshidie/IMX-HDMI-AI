#include "../include/camera.h"
#include "../include/display.h"
#include "../include/rga_process.h"
#include "../include/frame_queue.h"
#include "../include/common.h"
#include <sys/time.h>

#define DEV_CAMERA          "/dev/video11"
#define DEV_DRM             "/dev/dri/card0"
#define CAM_WIDTH           3840
#define CAM_HEIGHT          2160
#define CAM_BUFFER_COUNT    4
#define CAM_PIXEL_FMT       V4L2_PIX_FMT_NV12
#define CAM_BUF_TYPE        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE

#define CLIP(x)             ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))


typedef struct 
{
    FrameQueue* queue;
    DisplayContext* display;
    CameraContext* camera;
} appContext;


int system_init(appContext *ctx, const char *dev_name_camera, const char *dev_name_display, uint32_t w, uint32_t h, 
                uint32_t count, uint32_t pixel_format, uint32_t buf_type)
{
    camera_init(ctx->camera, dev_name_camera, w, h, count, pixel_format, buf_type);
    display_init(ctx->display, dev_name_display);
    frame_queue_init(ctx->queue);
    return 0;
}

int system_deinit(appContext* ctx)
{
    camera_deinit(ctx->camera);
    display_deinit(ctx->display);
    frame_queue_deinit(ctx->queue);
    return 0;
}

int main()
{
    appContext ctx;
    system_init(&ctx, DEV_CAMERA, DEV_DRM, CAM_WIDTH, CAM_HEIGHT, 
                            CAM_BUFFER_COUNT, CAM_PIXEL_FMT, CAM_BUF_TYPE);
    sleep(5);
    system_deinit(&ctx);
    return 0;
}