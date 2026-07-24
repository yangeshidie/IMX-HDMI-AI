# CameraToHdmi - 高性能多线程嵌入式视频采集与 DRM/RGA 渲染系统

本工程是基于 **嵌入式 Linux (Rockchip 平台)** 的多线程视频处理系统。项目采用 **生产者-消费者 (Producer-Consumer)** 异步多线程架构，将 **V4L2 视频采集** 与 **DRM/RGA 图像处理显示** 彻底解耦，并结合 **瑞芯微 RGA 2D 硬件加速** 与 **DMA-BUF 零拷贝 (Zero-Copy)** 技术，实现了高帧率、低延时且极致流畅的 4K 视频实时推屏显示。

---

## 目录

1. [系统特性](#系统特性)
2. [多线程系统架构图](#多线程系统架构图)
3. [目录与模块源码结构](#目录与模块源码结构)
4. [核心技术实现原理](#核心技术实现原理)
   - [异步多线程与帧队列同步](#1-异步多线程与帧队列同步)
   - [V4L2 多平面采集与 DMA-BUF 导出](#2-v4l2-多平面采集与-dma-buf-导出)
   - [DRM/KMS 无撕裂双缓冲 (Ping-Pong)](#3-drmkms-无撕裂双缓冲-ping-pong)
   - [RGA 2D 硬件 Blit 与零拷贝 Scaling](#4-rga-2d-硬件-blit-与零拷贝-scaling)
5. [编译与构建说明](#编译与构建说明)
6. [运行与部署指南](#运行与部署指南)
7. [优雅退出与资源清理](#优雅退出与资源清理)

---

## 系统特性

- **生产者-消费者异步解耦**：采集线程（Camera Thread）与显示线程（Display Thread）独立并行运行，避免了帧采集阻塞渲染或渲染延迟拖慢采集的问题。
- **线程安全帧队列 (Thread-Safe Queue)**：基于 `pthread_mutex` 与 `pthread_cond` 实现的环形 FIFO 帧缓冲区队列，支持安全唤醒与 `abort` 广播退出。
- **DRM 双缓冲 (Ping-Pong Double Buffering)**：维护 Front Buffer 与 Back Buffer 两个 Dumb Buffer，利用 `drmModePageFlip` 机制实现平滑交替翻页，彻底解决画面撕裂 (Tearing)。
- **RGA DMA-BUF 零拷贝**：直接利用 V4L2 与 DRM 导出的 Linux DMA-BUF 文件描述符 (FD)，由 RGA 硬件完成 NV12 到 BGRA8888 的颜色空间转换 (CSC) 及 4K 到屏幕分辨率的动态 Scaling，无 CPU 内存拷贝。
- **模块化面向对象设计**：高度模块化封装，上下文 (`CameraContext`, `DisplayContext`, `FrameQueue`) 结构清晰，接口简洁利于拓展。

---

## 多线程系统架构图

```
+-----------------------------------------------------------------------------------+
|                                 appContext 中枢                                   |
|                                                                                   |
|  +-----------------------+                    +--------------------------------+  |
|  |  cameraThread (生产者) |                    |     displayThread (消费者)     |  |
|  +-----------------------+                    +--------------------------------+  |
|              |                                                |                   |
|   camera_get_frame (DQBUF)                            frame_queue_pop             |
|              |                                                |                   |
|              v                                                v                   |
|   +---------------------+  pthread_cond/mutex  +------------------------------+   |
|   |  frame_queue_push   | -------------------> |  display_get_back_frame      |   |
|   +---------------------+  FrameQueue[size=8]  |  rga_process_convert_scale   |   |
|                                                |  display_show (Page Flip)    |   |
|                                                |  camera_put_frame (QBUF)     |   |
|                                                +------------------------------+   |
+-----------------------------------------------------------------------------------+
                                                                |
                                                                v
                                                       +------------------+
                                                       |   HDMI Monitor   |
                                                       +------------------+
```

---

## 目录与模块源码结构

项目源码分为头文件目录 `include/` 与实现文件目录 `src/`：

CameraToHdmi/
│
├── include/                     # 头文件目录
│   ├── [common.h](./include/common.h)         # 跨模块公共结构定义 (Frame)
│   ├── [camera.h](./include/camera.h)         # CameraContext 结构体及 V4L2 接口声明
│   ├── [display.h](./include/display.h)        # DisplayContext 结构体及 DRM 双缓冲接口声明
│   ├── [frame_queue.h](./include/frame_queue.h)   # 线程安全帧队列 (FrameQueue) 声明
│   └── [rga_process.h](./include/rga_process.h)   # 瑞芯微 RGA 硬件加速 Blit 函数接口
│
├── src/                         # 源文件目录
│   ├── [main.c](./src/main.c)           # 程序主入口、线程启动调度与 SIGINT 信号捕捉
│   ├── [camera.c](./src/camera.c)         # V4L2 多平面 4K NV12 采集与 DMA-BUF 导出实现
│   ├── [display.c](./src/display.c)        # DRM 连接器查找、Dumb Buffer 创建与翻页实现
│   ├── [frame_queue.c](./src/frame_queue.c)   # 互斥锁与条件变量控制的环形队列实现
│   └── [rga_process.c](./src/rga_process.c)   # c_RkRgaBlit 硬件图像格式转换与拉伸实现
│
└── [Makefile](./Makefile)                    # 交叉编译 Makefile，包含 deploy 及 run 规则

### 各模块职责详述：

| 模块文件                       | 核心结构 / API                                  | 功能描述                                                                                                                                                                               |
| :----------------------------- | :---------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`common.h`**         | `Frame`                                       | 统一传输单元，包含`dma_fd`、`width`、`height`、`format`、`index`、`timestamp`。                                                                                            |
| **`camera.h/.c`**      | `CameraContext`                               | 管理`/dev/video11` 节点。配置 `V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE` 4K NV12，使用 `VIDIOC_EXPBUF` 导出 DMA FD，提供 `camera_get_frame` (DQBUF) 与 `camera_put_frame` (QBUF)。 |
| **`display.h/.c`**     | `DisplayContext`                              | 管理`/dev/dri/card0` 节点。获取屏幕分辨率，创建 2 个 Dumb Buffer 并通过 `drmPrimeHandleToFD` 导出，提供 `display_get_back_frame` 与 `display_show` (Page Flip 翻页)。          |
| **`frame_queue.h/.c`** | `FrameQueue`                                  | 深度为 8 的环形帧队列。通过`pthread_mutex` 互斥保护，利用 `empty` 和 `full` 条件变量控制唤醒与阻塞，`frame_queue_abort` 实现安全解锁退出。                                     |
| **`rga_process.h/.c`** | `rga_process_convert_scale`                   | 配置`rga_info_t` 结构体，将摄像头 `src.fd` NV12 转换并拉伸为 DRM `dst.fd` BGRA8888 模式。                                                                                        |
| **`main.c`**           | `main()`, `cameraThread`, `displayThread` | 创建应用全局上下文`appContext`，初始化并创建采集与显示子线程。注册 `SIGINT` 信号安全退出并释放资源。                                                                               |

---

## 核心技术实现原理

### 1. 异步多线程与帧队列同步

- **采集线程 (`cameraThread`)**：无限循环阻塞式抓取摄像头视频帧。抓取成功后调用 `frame_queue_push` 将 `Frame` 元数据压入队列。如果队列已满 (容量 8)，线程将通过条件变量 `full` 挂起，直到显示线程消耗一帧。
- **显示线程 (`displayThread`)**：调用 `frame_queue_pop` 从队列获取待处理帧（若队列为空则通过条件变量 `empty` 挂起）。弹出一帧后：
  1. 获取 DRM 的后台缓冲区 (`display_get_back_frame`)。
  2. 调用 RGA 硬件 `rga_process_convert_scale` 将图像处理并写入后台缓冲区。
  3. 执行 `display_show` 进行 DRM 翻页。
  4. 调用 `camera_put_frame` 将处理完毕的摄像头缓冲区还给 V4L2 驱动以重复利用。

### 2. V4L2 多平面采集与 DMA-BUF 导出

- 使用 `V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE` 配置 4K (`3840x2160`) NV12 采集。
- 调用 `ioctl(fd, VIDIOC_EXPBUF, &expbuf)` 为分配的 4 个 `V4L2_MEMORY_MMAP` 缓冲区导出标准的 Linux DMA-BUF FD。
- 采集线程与显示线程间仅传递 FD 标号和轻量级 `Frame` 结构，无需对大容量 4K 图像内存数据进行任何复制。

### 3. DRM/KMS 无撕裂双缓冲 (Ping-Pong)

- 在初始化阶段成功分配 2 个 Dumb Buffer 显存。
- `front_buf_idx` 维护当前推屏显示的缓冲区，`back_buf_idx` 维护当前 RGA 正在绘制的离屏缓冲区。
- 调用 `drmModePageFlip`（或在无法 PageFlip 时降级使用 `drmModeSetCrtc`）实现无撕裂前后台缓冲区瞬间交换（Ping-Pong 切换）。

### 4. RGA 2D 硬件 Blit 与零拷贝 Scaling

- RGA 源配置：格式为 `RK_FORMAT_YCbCr_420_SP` (NV12)，裁剪区域为 `3840x2160`，关联 `src_frame->dma_fd`。
- RGA 目标配置：格式为 `RK_FORMAT_BGRA_8888`，裁剪区域为屏幕物理分辨率（如 `1024x600` 或 `1920x1080`），关联 `dst_frame->dma_fd`。
- 开启 `mmuFlag = 1`，调用 `c_RkRgaBlit(&src, &dst, NULL)` 纯硬件调度完成高效转换。

---

## 编译与构建说明

### 1. 编译环境配置

项目的 Makefile 依赖交叉编译工具链 `aarch64-linux-gnu-gcc` 以及相应的 SDK Sysroot：

- 默认 Sysroot：`SYSROOT = /home/yanmobile/my_software/RKSDK/TaishanPi-3-Linux/debian/binary`
- 关联头文件：`libdrm` (`/usr/include/libdrm`)、`librga`
- 依赖链接库：`-ldrm -lrga -lpthread`

### 2. 构建命令

在 `MultiThread/CameraToHdmi` 目录下执行：

```bash
# 编译生成可执行文件 TEST
make all

# 清理编译产物
make clean
```

---

## 运行与部署指南

### 1. 远程部署配置文件 (`../../config.mk`)

本工程包含 Makefile 部署规则，自动引入根目录下的 `config.mk` 配置：

```makefile
REMOTE_USER = user
REMOTE_IP = 10.xxx.xxx.41
REMOTE_PATH = /home/user
```

### 2. 部署与远程运行命令

```bash
# 自动传输最新编译的二进制 TEST 到远程开发板 (MultiThread 目录)
make deploy

# 一键推送并自动在远程开发板启动运行
make run
```

### 3. 本地运行 (目标板终端)

```bash
./TEST
```

*控制台输出示意：*

```text
DRM 显示初始化成功: 1024x600 (双缓冲就绪)
Display 线程启动...
```

---

## 优雅退出与资源清理

当按 `Ctrl+C` 发送 `SIGINT` 信号时，系统能够安全干净地退出：

1. 信号处理例程 `handle_sigint` 将 `g_quit` 设置为 `SIGNAL_QUIT`。
2. 主线程唤醒并调用 `frame_queue_abort(&ctx.queue)`，广播触发 `empty` 与 `full` 条件变量，解锁因队列等待而阻塞的子线程。
3. 主线程调用 `pthread_join` 等待 `cameraThread` 与 `displayThread` 优雅退出。
4. 调用 `system_deinit(&ctx)` 彻底清理：
   - 关闭摄像头视频流 `VIDIOC_STREAMOFF`。
   - 解除内存映射 `munmap` 并关闭所有 V4L2 与 DRM 导出的 DMA-BUF FD。
   - 销毁 DRM Dumb Buffer 并恢复保存的原始 CRTC 状态（`saved_crtc`），还原终端屏幕画面。
   - 销毁 `FrameQueue` 的互斥锁与条件变量。
