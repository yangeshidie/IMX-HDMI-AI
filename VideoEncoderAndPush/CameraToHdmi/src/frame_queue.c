#include "../include/frame_queue.h"

int frame_queue_init(FrameQueue *queue)
{
    if (!queue) return -1;

    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->abort = false;
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&queue->empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    if (pthread_cond_init(&queue->full, NULL) != 0) {
        pthread_cond_destroy(&queue->empty);
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    return 0; 
}
// 宏定义一下大小，假设你在头文件里定义了，比如 
// #define FRAME_QUEUE_SIZE 4

int frame_queue_push(FrameQueue *queue, const Frame *frame)
{
    if (queue == NULL || frame == NULL) return -1;
    pthread_mutex_lock(&queue->mutex);

    while (queue->count >= FRAME_QUEUE_SIZE && !queue->abort) {
        pthread_cond_wait(&queue->full, &queue->mutex);
    }
        if(queue->abort)
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    queue->frames[queue->tail] = *frame;
    queue->tail = (queue->tail + 1) % FRAME_QUEUE_SIZE;
    queue->count++;

    pthread_cond_signal(&queue->empty);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

int frame_queue_pop(FrameQueue *queue, Frame *frame)
{
    if (queue == NULL || frame == NULL) return -1;
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0 && !queue->abort) {
        pthread_cond_wait(&queue->empty, &queue->mutex);
    }
    if(queue->abort)
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    *frame = queue->frames[queue->head];
    queue->head = (queue->head + 1) % FRAME_QUEUE_SIZE;
    queue->count--;

    pthread_cond_signal(&queue->full);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}
int frame_queue_deinit(FrameQueue *queue)
{
    if (!queue) return -1;
    int ret = 0;
    if (pthread_cond_destroy(&queue->empty) != 0) {
        ret = -1;
    }
    if (pthread_cond_destroy(&queue->full) != 0) {
        ret = -1;
    }
    if (pthread_mutex_destroy(&queue->mutex) != 0) {
        ret = -1;
    }
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;

    return ret;
}

int frame_queue_abort(FrameQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->abort = true;
    pthread_cond_broadcast(&queue->empty);
    pthread_cond_broadcast(&queue->full);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}