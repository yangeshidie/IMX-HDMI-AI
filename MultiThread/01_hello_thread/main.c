#include <pthread.h>
#include <unistd.h>
#include <stdio.h>


void* threadMain1(void* arg)
{
    sleep(2);
    printf("线程1\n"); 
    sleep(2);
    printf("线程1退出\n");
    return NULL;       
}

void* threadMain2(void* arg)
{
    while(1)
    {
        printf("线程2\n"); 
        sleep(3);
    }
    return NULL;
}

int main()
{
    pthread_t myThread1;
    pthread_t myThread2;
    
    int erroNum = pthread_create(&myThread1, NULL, threadMain1, NULL);
    if(erroNum)
    {
        printf("创建线程1失败，错误码：%d\n", erroNum); 
        return -1;
    }
    
    erroNum = pthread_create(&myThread2, NULL, threadMain2, NULL);
    if(erroNum)
    {
        printf("创建线程2失败，错误码：%d\n", erroNum);
        return -1;
    }
    
    sleep(10);
    
    pthread_join(myThread1, NULL);
    pthread_join(myThread2, NULL); 
    
    return 0;
}