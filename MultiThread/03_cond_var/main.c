
// 实验设计要求：
// 声明一个全局共享资源：int g_box = 0;（0 表示空，1 表示满）。

// 声明一把互斥锁 myLock 和一个条件变量 myCond。

// 生产者线程：

// 加锁。

// 生产货物（把 g_box 变成 1）。

// 打印“【生产者】放入了货物。”

// 发送信号 pthread_cond_signal 唤醒消费者。

// 解锁，然后 sleep(2)（模拟生产间隔）。

// 消费者线程：

// 加锁。

// 用 while(g_box == 0) 判断，如果没货物，就调用 pthread_cond_wait 死等。

// 等醒来后，消费货物（把 g_box 变回 0）。

// 打印“【消费者】拿走了货物。”

// 解锁。

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

pthread_cond_t myCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;
int g_box = 0; 

void* productor(void* arg)
{
    for(int i = 0; i < 10; i++)
    {
        pthread_mutex_lock(&myMutex);
        
        g_box = 1; 
        printf("【生产者】放回了货物 [%d]\n", i + 1);
        
        pthread_cond_signal(&myCond);
        
        pthread_mutex_unlock(&myMutex); 
        
        sleep(1); 
    }
    return NULL;
}

void* consumer(void* arg)
{
    for(int i = 0; i < 10; i++)
    {
        pthread_mutex_lock(&myMutex); 
        
        while(g_box == 0)
        {
            pthread_cond_wait(&myCond, &myMutex);
        }
        
        g_box = 0; 
        printf("【消费者】拿走了货物 [%d]\n", i + 1);
        
        pthread_mutex_unlock(&myMutex); 
    }
    return NULL;
}

int main()
{
    pthread_t Productor;
    pthread_t Consumer;

    pthread_create(&Productor, NULL, productor, NULL);
    pthread_create(&Consumer, NULL, consumer, NULL);

    pthread_join(Productor, NULL);
    pthread_join(Consumer, NULL);

    printf("交易结束。\n");
    return 0;
}