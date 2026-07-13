#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

pthread_cond_t myCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;
int g_box = 0; 

volatile sig_atomic_t g_quit = 0;

void handle_sigint(int sig) {
    g_quit = 1; 
}

void* productor(void* arg)
{
    while(1)
    {
        pthread_mutex_lock(&myMutex);
        
        while(g_box == 1 && g_quit == 0)
        {
            pthread_cond_wait(&myCond, &myMutex);
        }
        
        if(g_quit == 1)
        {
            pthread_mutex_unlock(&myMutex);
            printf("【生产者】收到退出信号，正在清理资源并退出...\n");
            break;
        }
        
        g_box = 1; 
        printf("【生产者】放回了货物 \n");
        
        pthread_cond_broadcast(&myCond); 
        pthread_mutex_unlock(&myMutex); 
        
        sleep(1); 
    }
    return NULL;
}

void* consumer(void* arg)
{
    while(1)
    {
        pthread_mutex_lock(&myMutex); 
        
        while(g_box == 0 && g_quit == 0)
        {
            pthread_cond_wait(&myCond, &myMutex);
        }
        

        if(g_quit == 1)
        {
            pthread_mutex_unlock(&myMutex);
            printf("【消费者】收到退出信号，正在清理资源并退出...\n");
            break;
        }
        
        g_box = 0; 
        printf("【消费者】拿走了货物 \n");
        
        pthread_cond_broadcast(&myCond); 
        pthread_mutex_unlock(&myMutex); 
    }
    return NULL;
}

int main()
{
    signal(SIGINT, handle_sigint);

    pthread_t Productor;
    pthread_t Consumer;

    pthread_create(&Productor, NULL, productor, NULL);
    pthread_create(&Consumer, NULL, consumer, NULL);

    while(g_quit == 0) {
        usleep(100000); 
    }

    printf("\n【主线程】捕捉到 Ctrl+C 信号！准备通知子线程退出...\n");

    pthread_mutex_lock(&myMutex);
    pthread_cond_broadcast(&myCond);
    pthread_mutex_unlock(&myMutex);

    pthread_join(Productor, NULL);
    pthread_join(Consumer, NULL);

    printf("整个程序完美优雅退出，交易结束。\n");
    return 0;
}