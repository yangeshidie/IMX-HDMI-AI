# 01_hello_thread - POSIX 线程基础创建与管理

本目录包含 POSIX 线程 (pthreads) 的基础创建与等待实验，演示如何在 C 语言中创建轻量级线程 (LWP) 并管理其生命周期。

---

## 文件列表

- [main.c](./main.c) : 演示使用 `pthread_create` 创建两个子线程，并使用 `pthread_join` 等待线程结束。
- [Makefile](./Makefile) : 本地编译规则，自动链接 `-pthread` 库。

---

## 核心知识点

1. **线程创建**：使用 `pthread_create(&tid, NULL, func, arg)` 创建新线程。
2. **线程等待**：使用 `pthread_join(tid, NULL)` 阻塞等待指定线程运行结束，回收线程资源。
3. **线程函数**：线程入口函数需符合 `void* func(void* arg)` 签名。

---

## 编译与运行

在当前目录下执行：

```bash
# 编译生成可执行文件 main_exec
make

# 运行程序
make run

# 清理编译产物
make clean
```
