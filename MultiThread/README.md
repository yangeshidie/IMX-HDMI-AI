# MultiThread - Linux 多线程演进与嵌入式音视频实战工程

本目录包含一套从 **POSIX 线程基础** 到 **工程级多线程音视频实时处理** 的渐进式学习与实战项目。工程涵盖了线程创建与生命周期管理、互斥锁与竞态条件消除、条件变量协作通知、信号控制的优雅退出，以及结合 V4L2 摄像头采集与 DRM/RGA 硬件加速显示的 4K 多线程解耦综合实战框架。

---

## 目录

1. [模块演进路线图](#模块演进路线图)
2. [项目子目录索引](#项目子目录索引)
3. [核心技术演进流程](#核心技术演进流程)
4. [编译与部署指引](#编译与部署指引)
   - [本地子模块编译](#1-本地基础演进模块编译-01---04)
   - [综合实战项目部署 ( CameraToHdmi )](#2-综合实战项目远程部署--cameratohdmi-)

---

## 模块演进路线图

```
  +-----------------------+
  |  01_hello_thread      |  ---> 阶段一：POSIX 线程创建与等待 (pthread_create / join)
  +-----------------------+
              |
              v
  +-----------------------+
  |  02_mutex_lock        |  ---> 阶段二：消除数据竞争与加锁粒度对比 (pthread_mutex)
  +-----------------------+
              |
              v
  +-----------------------+
  |  03_cond_var          |  ---> 阶段三：线程间条件变量通知协同 (pthread_cond_wait / signal)
  +-----------------------+
              |
              v
  +-----------------------+
  |  04_elegant_exit      |  ---> 阶段四：信号捕捉 SIGINT 与工程级优雅退出 (pthread_cond_broadcast)
  +-----------------------+
              |
              v
  +-----------------------+
  |  CameraToHdmi         |  ---> 阶段五：实战集成！生产者-消费者模型 + V4L2 4K + RGA + DRM
  +-----------------------+
```

---

## 项目子目录索引

| 目录名称                                      | 演进阶段                       | 核心实现与学习目标                                                                        | 详细说明                                                |
| :-------------------------------------------- | :----------------------------- | :---------------------------------------------------------------------------------------- | :------------------------------------------------------ |
| [01_hello_thread](./01_hello_thread/README.md) | **阶段 1：线程基础**     | 学习使用 POSIX 接口创建子线程并使用`pthread_join` 进行等待，掌握线程生命周期。          | [01_hello_thread/README.md](./01_hello_thread/README.md) |
| [02_mutex_lock](./02_mutex_lock/README.md)     | **阶段 2：锁与数据竞争** | 对照测试无锁竞态条件、循环内频发加锁与临界区批量加锁对性能与数据准确性的影响。            | [02_mutex_lock/README.md](./02_mutex_lock/README.md)     |
| [03_cond_var](./03_cond_var/README.md)         | **阶段 3：条件变量**     | 引入`pthread_cond_t` 条件变量实现生产者-消费者模型，解决轮询消耗 CPU 的问题。           | [03_cond_var/README.md](./03_cond_var/README.md)         |
| [04_elegant_exit](./04_elegant_exit/README.md) | **阶段 4：优雅退出**     | 结合`signal(SIGINT)` 捕获 `Ctrl+C` 信号，通过广播强制唤醒挂起线程，实现零泄露清理。   | [04_elegant_exit/README.md](./04_elegant_exit/README.md) |
| [CameraToHdmi](./CameraToHdmi/README.md)       | **阶段 5：综合应用**     | 高性能异步多线程 4K 视频采集系统，包含 V4L2 采集、DRM 无撕裂双缓冲与 RGA DMA-BUF 零拷贝。 | [CameraToHdmi/README.md](./CameraToHdmi/README.md)       |

---

## 核心技术演进流程

1. **基础线程构建 (01_hello_thread)**
   通过 POSIX 线程 API 建立多任务并发基础，理解 CPU 调度与轻量级进程 (LWP) 概念。
2. **互斥锁与临界区保护 (02_mutex_lock)**
   分析并发写共享变量时的覆写覆盖现象，学会使用 `pthread_mutex_t` 保护临界区，并通过合理控制加锁粒度兼顾数据安全与极致性能。
3. **条件变量通知控制 (03_cond_var)**
   摒弃浪费 CPU 的死循环轮询，引入条件变量与等待队列，实现高效的“有资源即通知、无资源则挂起”机制。
4. **信号联动与优雅退出 (04_elegant_exit)**
   针对挂起在条件变量上的死锁问题，建立信号处理例程与广播唤醒联动机制，实现工程级无残留优雅清理。
5. **音视频解耦实战 (CameraToHdmi)**
   将基础知识融会贯通，应用在瑞芯微 Linux 平台上：构建 `cameraThread` 生产者与 `displayThread` 消费者，中间搭配 8 帧大小的线程安全环形缓冲区 `FrameQueue`，结合 DMA-BUF 零拷贝技术输出 4K 高清低延时视频流。

---

## 编译与部署指引

### 1. 本地基础演进模块编译 ( 01 - 04 )

进入对应的基础演进子目录（如 `01_hello_thread`）直接使用 `make` 本地编译与运行：

```bash
cd 01_hello_thread
make
make run
```

### 2. 综合实战项目远程部署 ( CameraToHdmi )

根目录及 `CameraToHdmi` 支持通过配置文件实现自动化远程编译推送：

#### 远程配置文件示例 (`../config.mk` 或同级配置)

```makefile
REMOTE_USER = user
REMOTE_IP = 10.xxx.xxx.41
REMOTE_PATH = /home/user
```

#### 交叉编译与一键推送命令

```bash
cd CameraToHdmi

# 交叉编译生成可执行目标 TEST
make all

# 一键推送最新二进制到开发板
make deploy

# 远程推送并在开发板自动启动运行
make run
```
