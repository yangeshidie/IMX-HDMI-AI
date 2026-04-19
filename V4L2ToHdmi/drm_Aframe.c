#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <linux/videodev2.h>

int main() {
    drmModeCrtc *saved_crtc = NULL;
    int fdDrm;
    drmModeRes *res;
    drmModeConnector *conn = NULL;
    drmModeEncoder *enc = NULL;
    drmModeModeInfo mode;
    uint32_t crtc_id;

    int fdCamera;
    v4l2_format fmt;
    v4l2_requestbuffers rqbuffer;
    v4l2_buffer buffer;
    
    /*************************open hdmi&camera********************************/
    fdDrm = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    fdCamera = open("/dev/video11", O_RDWR);
    if (fdDrm < 0) {
        perror("无法打开 /dev/dri/card0");
        return -1;
    }
    if (fdCamera < 0) {
        perror("无法打开 /dev/video0");
        return -1;
    }
    /*************************************************************************/
    // 2. 获取 DRM 资源
    res = drmModeGetResources(fdDrm);
    if (!res) {
        fprintf(stderr, "无法获取 DRM 资源\n");
        close(fdDrm);
        return -1;
    }

    /*****************************set format**********************************/
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;      // 告诉内核这是“捕获流”
    fmt.fmt.pix.width = 3840;                    // 4K 宽度
    fmt.fmt.pix.height = 2160;                   // 4K 高度
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12; // 你之前抓取成功的 NV12 格式
    fmt.fmt.pix.field = V4L2_FIELD_NONE;         // 逐行扫描
    if(ioctl(fdCamera, VIDIOC_S_FMT, &fmt) < 0){
        perror("格式设置失败");
        return -1;
    }
    /*************************************************************************/

    /**************************request buffer*********************************/
    memset(&rqbuffer, 0, sizeof(rqbuffer));
    rqbuffer.count = 4;
    rqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rqbuffer.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fdCamera, VIDIOC_REQBUFS, &rqbuffer) < 0) {
        perror("申请缓冲区失败");
        return -1;
    }
    if (rqbuffer.count < 2) {
        fprintf(stderr, "缓冲区数量不足，程序无法运行\n");
        return -1;
    }
    printf("内核实际分配了 %u 个缓冲区\n", rqbuffer.count);
    /*************************************************************************/

    // 3. 遍历 Connector，寻找处于 "已连接" 状态的屏幕 (你的 HDMI 屏幕)
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(fdDrm, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED) {
            printf("找到已连接的显示器: Connector ID = %d\n", conn->connector_id);
            break;
        }
        drmModeFreeConnector(conn);
        conn = NULL;
    }

    if (!conn || conn->count_modes == 0) {
        fprintf(stderr, "没有找到连接的显示器或无效的分辨率模式\n");
        return -1;
    }

    // 取显示器的首选分辨率 (默认是数组的第一个，也就是你屏幕的 1024x600)
    mode = conn->modes[0];
    printf("使用分辨率: %dx%d\n", mode.hdisplay, mode.vdisplay);

    // 4. 寻找 Encoder 和 CRTC (简化的查找逻辑，取当前绑定的 Encoder)
    enc = drmModeGetEncoder(fdDrm, conn->encoder_id);
    if (!enc) {
        fprintf(stderr, "无法获取 Encoder\n");
        return -1;
    }
    crtc_id = enc->crtc_id;
    saved_crtc = drmModeGetCrtc(fdDrm, crtc_id);
    if (!saved_crtc) {
        fprintf(stderr, "无法备份当前的 CRTC 状态\n");
        // 这里可以决定是退出还是继续
    }
    

    // 5. 创建 Dumb Buffer (显存)
    struct drm_mode_create_dumb create_arg = {0};
    create_arg.width = mode.hdisplay;
    create_arg.height = mode.vdisplay;
    create_arg.bpp = 32; // 色深 32-bit (XRGB8888)

    if (ioctl(fdDrm, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) < 0) {
        perror("创建 Dumb Buffer 失败");
        return -1;
    }
    printf("成功分配显存: size = %llu bytes, pitch = %u\n", create_arg.size, create_arg.pitch);

    // 6. 添加 Framebuffer (将显存注册为系统可用的 FB)
    uint32_t fb_id;
    if (drmModeAddFB(fdDrm, mode.hdisplay, mode.vdisplay, 24, 32, create_arg.pitch, create_arg.handle, &fb_id) < 0) {
        perror("添加 Framebuffer 失败");
        return -1;
    }

    // 7. 内存映射 (将 GPU 显存映射到 CPU 用户空间以便写入像素)
    struct drm_mode_map_dumb map_arg = {0};
    map_arg.handle = create_arg.handle;
    if (ioctl(fdDrm, DRM_IOCTL_MODE_MAP_DUMB, &map_arg) < 0) {
        perror("准备映射内存失败");
        return -1;
    }

    uint32_t *pixels = mmap(0, create_arg.size, PROT_READ | PROT_WRITE, MAP_SHARED, fdDrm, map_arg.offset);
    if (pixels == MAP_FAILED) {
        perror("mmap 失败");
        return -1;
    }

    // 8. 画纯色 (填充绿色)
    // XRGB8888 格式中，低到高字节依次是 B, G, R, X (忽略)
    // 所以 0x0000FF00 表示：R=0, G=255, B=0
    for (int i = 0; i < (create_arg.size / 4); i++) {
        pixels[i] = 0x0000FF00; 
    }

    // 9. 将画好绿色的 Framebuffer 推送到屏幕进行显示
    if (drmModeSetCrtc(fdDrm, crtc_id, fb_id, 0, 0, &conn->connector_id, 1, &mode) < 0) {
        perror("设置 CRTC 失败 (上屏失败)");
        return -1;
    }

    printf("已推送到屏幕，绿屏应该已显示。保持 5 秒钟...\n");
    sleep(5);
    if (saved_crtc) {
    printf("正在恢复原始屏幕配置...\n");
    // 注意：saved_crtc->mode 包含了原始的分辨率等信息
    if (drmModeSetCrtc(fdDrm, 
                        saved_crtc->crtc_id, 
                        saved_crtc->buffer_id, 
                        saved_crtc->x, 
                        saved_crtc->y, 
                        &conn->connector_id, 
                        1, 
                        &saved_crtc->mode) < 0) {
            perror("还原 CRTC 失败");
        } else {
            printf("恢复成功！\n");
        }
    }

    // 【步骤 3：释放备份内存】
    if (saved_crtc) {
        drmModeFreeCrtc(saved_crtc);
    }
    // 10. 资源清理 (可选，进程退出后内核也会自动回收)
    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    close(fdDrm);

    printf("测试结束。\n");
    return 0;
}