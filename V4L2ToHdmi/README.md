# V4L2ToHdmi - 嵌入式 Linux 视频采集与 DRM/HDMI 显示硬件加速系统

本工程提供了一套基于 **嵌入式 Linux (Rockchip 平台)** 的渐进式视频采集与显示测试方案。项目涵盖了从基础设备探查、DRM 基础显示、V4L2 4K 图像采集、CPU 软件格式转换缩放，到结合 **瑞芯微 RGA (2D Graphic Acceleration Engine)** 与 **DMA-BUF 零拷贝 (Zero-Copy)** 硬件加速渲染的全过程。

---

## 目录

1. [项目特性](#项目特性)
2. [技术架构图](#技术架构图)
3. [源码文件与渐进式演进路线](#源码文件与渐进式演进路线)
4. [核心技术实现原理](#核心技术实现原理)
5. [环境依赖与配置](#环境依赖与配置)
6. [编译与部署说明](#编译与部署说明)
7. [运行与测试指南](#运行与测试指南)
8. [常见问题排查](#常见问题排查)

---

## 项目特性

- **多平面 V4L2 视频采集**：支持 4K (`3840x2160`) NV12 格式的高分辨率视频流采集 (`V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`)。
- **DRM/KMS 高效显示**：直接操作 DRM Dumb Buffer 与 Connector/CRTC，绕过 X11/Wayland 等复杂图形框架，实现超低延迟 HDMI 推屏。
- **硬件 2D 加速 (RGA)**：集成瑞芯微 RGA 硬件加速引擎，高效处理 NV12 到 XRGB8888/BGRA8888 的颜色空间转换 (CSC) 及任意比例图像缩放。
- **DMA-BUF 零拷贝传输**：结合 `VIDIOC_EXPBUF` 与 `drmPrimeHandleToFD` 导出内存句柄，实现摄像头帧缓冲区到显示显存的零 CPU 内存拷贝加速。
- **自动化编译与远程部署**：Makefile 内置 SCP/SSH 联动命令，支持一键部署到目标开发板并自动远程启动执行。

---

## 技术架构图

```
+-------------------+        DMA-BUF (FD)        +-------------------+
|  V4L2 Camera      | -------------------------> |  Rockchip RGA 2D  |
|  /dev/video11     |                            |  Hardware Engine  |
|  (4K NV12 Multi)  |                            +-------------------+
+-------------------+                                      |
                                                           | DMA-BUF (FD)
                                                           v
+-------------------+       DRM SetCRTC          +-------------------+
|  HDMI Monitor     | <------------------------- |  DRM Dumb Buffer  |
|  (1024x600/1080p) |                            |  /dev/dri/card0   |
+-------------------+                            +-------------------+
```

---

## 源码文件与渐进式演进路线

项目源码采用模块化渐进设计，方便一步步定位硬件通路与性能瓶颈：

| 文件名                                                                                               | 对应生成目标           | 阶段定位                              | 功能描述                                                                                                     |
| :--------------------------------------------------------------------------------------------------- | :--------------------- | :------------------------------------ | :----------------------------------------------------------------------------------------------------------- |
| [camera_test.c](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/camera_test.c)           | `TEST_DEVICE_CAMERA` | **阶段 1：设备探查**            | 探测摄像头节点（如`/dev/video11`），查询单平面/多平面 (MPLANE) 模式下的长宽分辨率及像素格式。              |
| [drm_green.c](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/drm_green.c)               | `TEST_PURE_GREEN`    | **阶段 2：DRM 通路验证**        | 初始化 DRM/KMS 通路，申请 Dumb Buffer 显存并由 CPU 填充纯绿像素推送上屏，5 秒后自动还原原始屏幕状态。        |
| [drm_Aframe.c](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/drm_Aframe.c)             | `TEST_A_FRAME`       | **阶段 3：V4L2+DRM CPU联调**    | 采集单帧 4K NV12 图像，通过**CPU 纯软件算法** 进行 NV12 到 XRGB8888 的格式转换与图像缩放，并推送显示。 |
| [drm_Aframe_RGA.c](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/drm_Aframe_RGA.c)     | `TEST_A_FRAME_RGA`   | **阶段 4：CPU vs RGA 单帧对比** | 引入 RGA 硬件 Blit 加速与 DMA-BUF 交互，高精度测量并输出单帧场景下 CPU 与 RGA 的转换耗时对比。               |
| [final_video_test.c](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/final_video_test.c) | `FINAL`              | **阶段 5：综合压测与长测**      | 包含 15s 连续视频流实时测试、单帧极限吞吐量压测及 15 分钟 RGA 连续稳定性长测。支持参数化切换。               |
| [Makefile](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/Makefile)                     | N/A                    | **构建与部署工程**              | 交叉编译 Makefile，包含依赖链接配置及自动 SCP 推送与 SSH 远程一键运行工具链。                                |

---

## 核心技术实现原理

### 1. V4L2 多平面 Capture 流程

为了支持高分辨率（如 4K）摄像头驱动，程序使用 `V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE` 模式进行视频捕捉：

- 使用 `ioctl(..., VIDIOC_S_FMT, &fmt)` 设置 4K NV12 像素格式。
- 使用 `ioctl(..., VIDIOC_REQBUFS, &rqp)` 申请 4 个 V4L2 帧缓冲区。
- 使用 `ioctl(..., VIDIOC_EXPBUF, &expbuf)` 将 V4L2 缓冲区导出为标准的 Linux DMA-BUF 文件描述符 (FD)。

### 2. DRM/KMS Dumb Buffer 显示控制

- 通过 `drmModeGetResources` 检索系统 DRM 设备节点（`/dev/dri/card0`）。
- 自动搜索状态为 `DRM_MODE_CONNECTED` 的显示连接器 (Connector)。
- 调用 `DRM_IOCTL_MODE_CREATE_DUMB` 申请对应分辨率的硬件显存。
- 使用 `drmModeSetCrtc` 设置屏幕显示上下文，并在退出时保存与还原原始 CRTC 状态。

### 3. DMA-BUF 与 RGA 硬件 2D 加速 (Zero-Copy)

在 [drm_Aframe_RGA.c](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/drm_Aframe_RGA.c) 及 [final_video_test.c](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/final_video_test.c) 中：

- 摄像头帧缓冲区与 DRM 显存均导出为 DMA-BUF FD (`src.fd` 和 `dst.fd`)。
- 硬件 2D 加速核心代码调用瑞芯微 RGA API `c_RkRgaBlit(&src, &dst, NULL)`。
- 由 RGA 硬件以 DMA 方式直接读取摄像头显存、完成格式转换与图像拉伸，并直接写入 DRM 显示显存，无需 CPU 参与内存搬运。

---

## 环境依赖与配置

### 编译依赖

- 交叉编译器：`aarch64-linux-gnu-gcc`
- 依赖头文件：`libdrm` (`/usr/include/libdrm`)、`librga` (`rga/RgaApi.h`)
- 依赖动态库：`-ldrm`, `-lrga`

### 远程配置文件 (`config.mk`)

在根目录下提供 `config.mk`（或参考 `config.mk.examole`）用于定义远程目标开发板的地址与路径：

```makefile
REMOTE_USER = user
REMOTE_IP = 10.xxx.xxx.41
REMOTE_PATH = /home/user
```

---

## 编译与部署说明

在 `V4L2ToHdmi` 目录下执行以下 Make 指令：

### 1. 编译全部可执行目标

```bash
make all
```

编译成功后将输出以下二进制文件：

- `TEST_DEVICE_CAMERA`
- `TEST_PURE_GREEN`
- `TEST_A_FRAME`
- `TEST_A_FRAME_RGA`
- `FINAL`

### 2. 清理编译产物

```bash
make clean
```

### 3. 一键推送最新二进制到开发板

```bash
make deploy
```

*自动在目标路径下的 `CameraToHdmi/` 目录存放最新编译的可执行文件。*

### 4. 远程部署并立即运行

```bash
make run
```

*自动推送最新编译的目标程序并通过 SSH 远程赋予执行权限并直接启动运行。*

---

## 运行与测试指南

### 单项测试程序

在开发板终端直接执行对应的二进制文件：

- `./TEST_DEVICE_CAMERA`：检查摄像头支持的分辨率和模式。
- `./TEST_PURE_GREEN`：测试 HDMI 显示是否正常输出绿屏。
- `./TEST_A_FRAME`：测试 CPU 模式下的单帧视频渲染（按下 Enter 键退出）。
- `./TEST_A_FRAME_RGA`：执行 CPU 与 RGA 单帧格式转换耗时对比测试。

### 综合压测程序 `FINAL`

[final_video_test.c](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/final_video_test.c) 生成的程序 `FINAL` 支持通过命令行参数传入指定的测试模式：

```bash
./FINAL [测试模式]
```

#### 可选参数说明：

- **`0` - 视频流实时采集测试 (Streaming Test)**：
  进行 15 秒实时视频采集推屏，依次对比 CPU 与 RGA 的实时渲染 FPS 与单帧平均延迟。
- **`1` - 极限吞吐量压测 (Limit Benchmark)**：
  锁定缓存中的单帧 4K 图像连续不停交给 CPU / RGA 计算，测试硬件算力的理论极限帧率与最小延迟。
- **`2` - 全部测试 (Default)**：
  顺序运行模式 `0` 与模式 `1`。
- **`3` - 15分钟 RGA 连续长测 (Long-Time Stream)**：
  持续运行 15 分钟 RGA 硬件加速连续视频流推屏，测试高负载场景下的系统稳定性与帧率波动。

*示例：*

```bash
# 仅执行极限压测
./FINAL 1

# 执行 15 分钟稳定性长测
./FINAL 3
```

---

## 常见问题排查

1. **打开设备失败 (`/dev/video11` 或 `/dev/dri/card0`)**
   - 请检查摄像头节点是否正确绑定。由于不同的 Rockchip BSP 驱动配置不同，摄像头节点可能是 `/dev/video0`、`/dev/video8` 或 `/dev/video11`，可在 [camera_test.c](file:///C:/Users/Mobile/Desktop/32165/IMX-HDMI-AI/V4L2ToHdmi/camera_test.c) 或配置头中进行修正。
   - 请确保运行身份具备 root 权限或关联设备文件权限。
2. **RGA Blit 失败**
   - 检查系统 `librga` 驱动及核心模块是否正常加载（如 `/dev/rga` 节点是否存在）。
   - 检查 V4L2 导出 DMA FD 时是否包含了物理连续的内存块。
3. **显示器没有输出或画面倾斜**
   - 请确认 DRM Connector 已经处于连接状态，且分辨率与屏幕硬件完全匹配（默认根据 DRM Connector 最佳 mode 决定）。
