#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#define ADD_TIMES 1000000

long gCount = 0;
pthread_mutex_t myLock = PTHREAD_MUTEX_INITIALIZER;

void* threadMain1(void* arg)
{
    for(int i = 0; i < ADD_TIMES; i++)
    {
        gCount++;
    }
    return NULL;
}

void* threadMain2(void* arg)
{
    for(int i = 0; i < ADD_TIMES; i++)
    {
        pthread_mutex_lock(&myLock);
        gCount++;
        pthread_mutex_unlock(&myLock);
    }
    return NULL;
}

void* threadMain3(void* arg)
{
    pthread_mutex_lock(&myLock); // 进函数直接锁死
    for(int i = 0; i < ADD_TIMES; i++)
    {
        gCount++;
    }
    pthread_mutex_unlock(&myLock); // 算完百万次再开锁
    return NULL;
}

void addWithoutMutex()
{
    struct timeval begin, end;
    pthread_t myThread1;
    pthread_t myThread2;

    gettimeofday(&begin, 0);
    int erroNum = pthread_create(&myThread1, NULL, threadMain1, NULL);
    if(erroNum) { printf("创建线程1失败，错误码：%d\n", erroNum); }
    
    erroNum = pthread_create(&myThread2, NULL, threadMain1, NULL);
    if(erroNum) { printf("创建线程2失败，错误码：%d\n", erroNum); }
    
    pthread_join(myThread1, NULL);
    pthread_join(myThread2, NULL);
    gettimeofday(&end, 0);

    long seconds = end.tv_sec - begin.tv_sec;
    long microseconds = end.tv_usec - begin.tv_usec;
    double elapsed_ms = (seconds * 1000.0) + (microseconds / 1000.0); // 统一换算成毫秒
    
    printf("不加锁1：时间：%.2f ms\n", elapsed_ms);
    printf("不加锁1：gCount=%ld\n", gCount);
    gCount = 0;
    return;
}

void addWithMutex()
{
    struct timeval begin, end;
    pthread_t myThread1;
    pthread_t myThread2;
    
    // --- 测试锁2 (循环内加锁) ---
    gettimeofday(&begin, 0);
    int erroNum = pthread_create(&myThread1, NULL, threadMain2, NULL);
    if(erroNum) { printf("创建线程1失败，错误码：%d\n", erroNum); }
    
    erroNum = pthread_create(&myThread2, NULL, threadMain2, NULL);
    if(erroNum) { printf("创建线程2失败，错误码：%d\n", erroNum); }
    
    pthread_join(myThread1, NULL);
    pthread_join(myThread2, NULL);
    gettimeofday(&end, 0);
    
    long seconds = end.tv_sec - begin.tv_sec;
    long microseconds = end.tv_usec - begin.tv_usec;
    double elapsed_ms = (seconds * 1000.0) + (microseconds / 1000.0);
    
    printf("加锁2（循环内加锁）：时间：%.2f ms\n", elapsed_ms);
    printf("加锁2（循环内加锁）：gCount=%ld\n", gCount);
    
    pthread_mutex_lock(&myLock);
    gCount = 0;
    pthread_mutex_unlock(&myLock);

    // --- 测试锁3 (循环外加锁) ---
    gettimeofday(&begin, 0);
    erroNum = pthread_create(&myThread1, NULL, threadMain3, NULL);
    if(erroNum) { printf("创建线程1失败，错误码：%d\n", erroNum); }
    
    erroNum = pthread_create(&myThread2, NULL, threadMain3, NULL);
    if(erroNum) { printf("创建线程2失败，错误码：%d\n", erroNum); }
    
    pthread_join(myThread1, NULL);
    pthread_join(myThread2, NULL);
    gettimeofday(&end, 0);
    
    seconds = end.tv_sec - begin.tv_sec;
    microseconds = end.tv_usec - begin.tv_usec;
    elapsed_ms = (seconds * 1000.0) + (microseconds / 1000.0); // 【修正】去掉开头的 double，直接赋值
    
    printf("加锁3（循环外加锁）：时间：%.2f ms\n", elapsed_ms);
    printf("加锁3（循环外加锁）：gCount=%ld\n", gCount);
    
    pthread_mutex_lock(&myLock);
    gCount = 0;
    pthread_mutex_unlock(&myLock);
    return;
}

int main()
{
    addWithoutMutex();
    printf("---------------------------\n");
    addWithMutex();
    return 0;
}