# CameraToHdmi - 多通道并发视频采集、DRM/RGA 渲染与 MPP 硬件编码系统

本工程是针对 **嵌入式 Linux (Rockchip 平台)** 的高性能多管道音视频处理系统。相较于基础的纯显示框架，本工程引入了 **瑞芯微 MPP (Media Process Platform) 硬件 H.264 视频编码器**、**多消费者并发解耦管道**、**基于原子操作的跨线程帧引用计数管理** 以及 **非阻塞实时丢帧队列**，实现了 4K 视频采集、低延时 HDMI 显示与高清 H.264 硬件录像/推流的并行处理。

---

## 目录

1. [系统演进与核心差异对比](#系统演进与核心差异对比)
2. [多管道系统架构图](#多管道系统架构图)
3. [目录与源码结构](#目录与源码结构)
4. [核心突破与技术原理](#核心突破与技术原理)
   - [1. 瑞芯微 MPP 硬件 H.264 编码 (Zero-Copy)](#1-瑞芯微-mpp-硬件-h264-编码-zero-copy)
   - [2. 原子引用计数帧生命周期管理](#2-原子引用计数帧生命周期管理)
   - [3. 多消费者并发解耦管道](#3-多消费者并发解耦管道)
   - [4. 非阻塞实时覆盖丢帧队列](#4-非阻塞实时覆盖丢帧队列)
5. [编译与部署说明](#编译与部署说明)
6. [运行与资源清理](#运行与资源清理)

---

## 系统演进与核心差异对比

本模块与基础多线程显示框架 (`MultiThread/CameraToHdmi`) 的主要改进与差异如下：

| 特性维度             | 基础显示框架 (`MultiThread`)                       | 本工程 (`VideoEncoderAndPush`)                                | 架构优势与改进说明                                                            |
| :------------------- | :--------------------------------------------------- | :-------------------------------------------------------------- | :---------------------------------------------------------------------------- |
| **功能扩展**   | 仅支持 DRM 显示输出                                  | 支持**DRM 显示 + MPP H.264 硬件编码**                     | 新增 MPP 硬件编码模块，可同时输出 HDMI 画面并录制/推送 H.264 码流。           |
| **管道拓扑**   | 单一消费通道 (`cameraThread` -> `displayThread`) | **多消费者并发管道** (`Display` + `Encoder` + `AI`) | 通过宏开关独立控制显示、编码与 AI 推理线程的并行工作。                        |
| **帧生命周期** | 单线程独占归还 (QBUF)                                | **原子引用计数 (`frame_retain` / `release`)**         | 基于`stdatomic.h` 记录单帧被多少消费者引用，归零时安全自动归还 V4L2 驱动。  |
| **帧队列机制** | 阻塞式 FIFO 队列 (满时卡死生产者)                    | **非阻塞覆盖丢帧队列** (Non-blocking Drop)                | 消费者处理跟不上时自动覆盖丢弃旧帧，确保`cameraThread` 核心采集流畅无卡顿。 |
| **依赖动态库** | `-ldrm -lrga -lpthread`                            | **`-ldrm -lrga -lrockchip_mpp -lpthread`**              | 引入`librockchip_mpp` 进行零拷贝硬件视频压缩。                              |

---

## 多管道系统架构图

```
+-----------------------------------------------------------------------------------------------+
|                                      appContext 主控中枢                                      |
|                                                                                               |
|                               +------------------------------+                                |
|                               |     cameraThread (采集主线程)  |                                |
|                               +------------------------------+                                |
|                                              |                                                |
|                                  camera_get_frame (DQBUF)                                     |
|                                  frame_retain(&frame)                                         |
|                                              |                                                |
|                   +--------------------------+--------------------------+                     |
|                   | (THREAD_DISPLAY)         | (THREAD_ENCODER)         | (THREAD_AI)         |
|                   v                          v                          v                     |
|          +------------------+       +------------------+       +------------------+           |
|          | display_q (Cap=4)|       |  encode_q (Cap=4)|       |    ai_q (Cap=1)  |           |
|          +------------------+       +------------------+       +------------------+           |
|                   |                          |                          |                     |
|                   v                          v                          v                     |
|          +------------------+       +------------------+       +------------------+           |
|          |  displayThread   |       |  encoderThread   |       |    aiThread      |           |
|          |  RGA Blit + DRM  |       |  MPP H.264 Encode|       |  (预留扩展接口)  |           |
|          +------------------+       +------------------+       +------------------+           |
|                   |                          |                          |                     |
|            frame_release              frame_release              frame_release                |
|                   +--------------------------+--------------------------+                     |
|                                              |                                                |
|                             ref_count == 0 -> camera_put_frame (QBUF)                         |
+-----------------------------------------------------------------------------------------------+
```

---

## 目录与源码结构

```
CameraToHdmi/
│
├── include/                     # 头文件目录
│   ├── [common.h](./include/common.h)         # 公共结构体 Frame、多线程宏定义与引用计数 API
│   ├── [camera.h](./include/camera.h)         # V4L2 摄像头上下文 CameraContext 及 Buffer 结构
│   ├── [display.h](./include/display.h)        # DRM 显示上下文 DisplayContext 与双缓冲管理
│   ├── [frame_queue.h](./include/frame_queue.h)   # 非阻塞覆盖丢帧队列 FrameQueue 接口
│   ├── [mpp_encoder.h](./include/mpp_encoder.h)   # 瑞芯微 MPP H.264 编码器上下文 EncoderContext
│   └── [rga_process.h](./include/rga_process.h)   # RGA 2D 硬件 Blit 缩放与格式转换接口
│
├── src/                         # 源文件目录
│   ├── [main.c](./src/main.c)           # 多通道线程调度主入口、信号捕捉与生命周期控制
│   ├── [camera.c](./src/camera.c)         # V4L2 多平面采集、DMA-BUF 导出与原子引用计数实现
│   ├── [display.c](./src/display.c)        # DRM 双缓冲创建、Dumb Buffer 导出与 Page Flip 翻页
│   ├── [frame_queue.c](./src/frame_queue.c)   # 支持动态容量与丢帧策略的环形队列实现
│   ├── [mpp_encoder.c](./src/mpp_encoder.c)   # MPP H.264 编码初始化、DMA-BUF 零拷贝导入与打包输出
│   └── [rga_process.c](./src/rga_process.c)   # c_RkRgaBlit 图像处理实现
│
├── [generate_xml.sh](./generate_xml.sh)         # 工程代码 XML 结构打包工具脚本
└── [Makefile](./Makefile)                    # 交叉编译 Makefile（包含 -lrockchip_mpp）
```

---

## 核心突破与技术原理

### 1. 瑞芯微 MPP 硬件 H.264 编码 (Zero-Copy)

- 在 [mpp_encoder.c](./src/mpp_encoder.c) 中，通过 `mpp_create` 与 `mpp_init` 配置为 `MPP_VIDEO_CodingAVC` 编码器，并开启 CBR 码率控制与 10Mbps 目标码率。
- **零拷贝 DMA-BUF 导入**：利用 `mpp_buffer_import_with_tag` 将 V4L2 导出的 `in_frame->dma_fd` 直接绑定到 `MppBuffer`，无需 CPU 进行大内存搬运即可推入编码队列。
- 编码产物自动提取 SPS/PPS 头部并写入 `record_output.h264`。

### 2. 原子引用计数帧生命周期管理

在 [common.h](./include/common.h) 与 [camera.c](./src/camera.c) 中引入 `atomic_int ref_count`：

- 当 `camera_get_frame` 捕获新帧时，初始化 `ref_count = 1`。
- `cameraThread` 每向一个消费队列（如 `display_q` 或 `encode_q`）投递该帧，均调用 `frame_retain(&frame)` 使 `ref_count++`。
- 消费线程处理完成后各自调用 `frame_release(&frame)` 使 `ref_count--`。
- 当 `ref_count` 减至 `0` 时，自动触发 `camera_put_frame` 执行 `VIDIOC_QBUF` 归还底层驱动，彻底避免内存撕裂或提前归还造成的画面花屏。

### 3. 多消费者并发解耦管道

在 [common.h](./include/common.h) 中提供了灵活的开关控制宏：

```c
#define THREAD_DISPLAY      THREAD_ON   // 开启 DRM/HDMI 显示输出
#define THREAD_ENCODER      THREAD_ON   // 开启 MPP H.264 硬件编码
#define THREAD_AI           THREAD_OFF  // 预留 AI 推理通道
```

在 [main.c](./src/main.c) 中根据宏配置并发创建 `displayThread` 与 `encoderThread`，各线程互不干扰。

### 4. 非阻塞实时覆盖丢帧队列

针对编码或显示偶尔耗时波动的场景，[frame_queue.c](./src/frame_queue.c) 移除了 `full` 条件变量阻塞：

- 当队列满时，`frame_queue_push` 自动弹出最旧的一帧释放（`frame_release`），并将最新帧压入。
- 保证了摄像头采集线程 `cameraThread` 的实时性，绝不会因某个后台任务卡顿而拖慢整个系统。

---

## 编译与部署说明

### 1. 依赖环境与 Sysroot

- 交叉编译器：`aarch64-linux-gnu-gcc`
- 链接依赖库：`-ldrm -lrga -lrockchip_mpp -lpthread`
- 默认 SDK 路径配置见 [Makefile](./Makefile) 中的 `SYSROOT` 定义。

### 2. 远程部署配置示例 (`../../config.mk`)

```makefile
REMOTE_USER = user
REMOTE_IP = 10.xxx.xxx.41
REMOTE_PATH = /home/user
```

### 3. 构建与部署命令

在当前 `CameraToHdmi` 目录下执行：

```bash
# 编译生成可执行文件 TEST
make all

# 清理构建产物
make clean

# 一键推送二进制到远程开发板
make deploy

# 远程推送并在开发板启动运行
make run
```

---

## 运行与资源清理

运行程序时终端将打印输出各并发线程的启动状态与硬件版本：

```text
x.y.z：[版本号概述]
DRM 显示初始化成功: 1024x600 (双缓冲就绪)
[Camera] 线程已启动...
[Display] 线程已启动...
[Encoder] 线程已启动...
```

程序捕捉到 `Ctrl+C` (`SIGINT`) 信号时，主线程广播终止队列，等待各线程安全退出后解绑 V4L2 缓冲区、销毁 MPP 编码器上下文与恢复原始 DRM CRTC 显示，保障硬件资源干净回收。
