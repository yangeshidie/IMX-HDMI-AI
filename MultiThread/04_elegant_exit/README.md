# 04_elegant_exit - 信号捕捉与多线程优雅退出

本目录展示如何在工程应用中捕捉操作系统中断信号 (`SIGINT` / `Ctrl+C`)，并广播通知挂起在条件变量中的所有子线程，实现零资源泄漏的优雅安全退出 (Graceful Exit)。

---

## 文件列表

- [main.c](./main.c) : 注册 `SIGINT` 信号处理例程，结合原子退出标记 `g_quit` 与 `pthread_cond_broadcast` 实现优雅退出。
- [Makefile](./Makefile) : 本地编译构建脚本。

---

## 优雅退出流程原理

1. **信号捕获**：主线程使用 `signal(SIGINT, handle_sigint)` 捕获 `Ctrl+C` 中断，设置全局退出标志位 `g_quit = 1`。
2. **条件变量唤醒**：在条件变量等待循环条件中增加 `g_quit == 0` 判断。主线程调用 `pthread_cond_broadcast(&myCond)` 强制唤醒所有处于 `pthread_cond_wait` 挂起状态的子线程。
3. **线程自清理与回收**：子线程被唤醒后检测到 `g_quit == 1`，自行解锁、清理本地资源并退出；主线程调用 `pthread_join` 完成资源回收。

---

## 编译与运行

```bash
# 编译生成 EXIT_ELEGANT
make

# 本地运行测试 (运行中可按 Ctrl+C 测试优雅退出)
make run

# 清理
make clean
```
