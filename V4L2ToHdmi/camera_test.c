#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <unistd.h>

int main() {
    int fd = open("/dev/video11", O_RDWR); // 这里通常是 video0，也可能是 video8 (RK平台特有)
    if (fd < 0) { perror("打开失败"); return -1; }

    // 结构体：查询设备能力
    struct v4l2_format fmt = {0};

    // 先尝试传统的单平面模式
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
        printf("检测到单平面格式: %d x %d\n", fmt.fmt.pix.width, fmt.fmt.pix.height);
    } else {
        // 如果失败，尝试瑞芯微常用的多平面模式 (MPLANE)
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (ioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
            printf("检测到多平面格式 (MPLANE): %d x %d\n", 
                    fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height);
        } else {
            printf("查询分辨率失败: %s\n", strerror(errno));
        }
    }
}