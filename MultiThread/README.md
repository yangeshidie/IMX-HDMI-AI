MultiThread/
│
├── 01_hello_thread/            # 阶段一：初识 LWP
│   ├── main.c                # 实验：只创建 1-2 个基础线程，打印它们的 LWP 号和进程 PID
│   └── Makefile                # 学习如何链接 -lpthread 库进行编译
│
├── 02_mutex_lock/              # 阶段二：体验数据竞争与加锁
│   ├── main.c                # 实验：两个线程同时对一个全局变量 ++ 到 100万，看没锁时数据怎么崩，加锁后怎么变正常
│   └── Makefile
│
├── 03_cond_var/                # 阶段三：线程间的步调协同（通知机制）
│   ├── main.c                # 实验：编写最基础的生产者与消费者，学习 wait 和 signal，搞懂伪唤醒
│   └── Makefile
│
├── 04_elegant_exit/            # 阶段四：掌握工程级控场能力
│   ├── main.c                # 实验：引入信号捕捉（Ctrl+C），让死等在条件变量里的线程全部安全释放
│   └── Makefile
│
└── CameraToHdmi/               #实战代码：构建生产者消费者模型。