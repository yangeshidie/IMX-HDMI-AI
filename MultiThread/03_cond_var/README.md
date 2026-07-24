# 03_cond_var - 条件变量 (Condition Variable) 协作通知

本目录包含基于条件变量 (`pthread_cond_t`) 与互斥锁 (`pthread_mutex_t`) 实现的单缓冲区生产者-消费者模型，展示多线程间高效协同与通知机制。

---

## 文件列表

- [main.c](./main.c) : 实现生产者线程生成货物并发送唤醒信号，消费者线程在没有货物时挂起等待。
- [Makefile](./Makefile) : 本地编译脚本。

---

## 核心技术点

1. **解决轮询消耗**：当缺乏资源时，消费者使用 `pthread_cond_wait(&myCond, &myMutex)` 进入休眠，避免占用 CPU 资源。
2. **唤醒通知**：生产者准备好资源后，调用 `pthread_cond_signal(&myCond)` 单播唤醒等待中的消费者。
3. **防虚假唤醒**：必须使用 `while(g_box == 0)` 循环校验条件，防止被操作系统虚假唤醒 (Spurious Wakeup) 导致逻辑异常。

---

## 编译与运行

```bash
# 编译生成 COND_VAR
make

# 本地运行
make run

# 清理
make clean
```
