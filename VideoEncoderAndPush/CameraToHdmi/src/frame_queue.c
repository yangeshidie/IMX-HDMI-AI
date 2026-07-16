#include "../include/frame_queue.h"
#include <stdlib.h>

int frame_queue_init(FrameQueue *queue, int capacity)
{
    if (!queue || capacity <= 0) return -1;

    queue->frames = (Frame *)malloc(sizeof(Frame) * capacity);
    if (!queue->frames) return -1;

    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->abort = false;

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->frames);
        return -1;
    }
    if (pthread_cond_init(&queue->empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->frames);
        return -1;
    }
    return 0; 
}

int frame_queue_push(FrameQueue *queue, const Frame *frame)
{
    if (queue == NULL || frame == NULL) return -1;
    
    Frame dropped_frame;
    bool did_drop = false;

    pthread_mutex_lock(&queue->mutex);

    if(queue->abort) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    if (queue->count >= queue->capacity) {
        dropped_frame = queue->frames[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        queue->count--;
        did_drop = true; // 标记我们踢掉了一个老帧
    }

    queue->frames[queue->tail] = *frame;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    pthread_cond_signal(&queue->empty);
    pthread_mutex_unlock(&queue->mutex);

    if (did_drop) {
        frame_release(&dropped_frame);
    }

    return 0;
}

int frame_queue_pop(FrameQueue *queue, Frame *frame)
{
    if (queue == NULL || frame == NULL) return -1;
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0 && !queue->abort) {
        pthread_cond_wait(&queue->empty, &queue->mutex);
    }
    if(queue->abort) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    *frame = queue->frames[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

int frame_queue_deinit(FrameQueue *queue)
{
    if (!queue) return -1;
    
    // 如果队列里还有没处理完的帧，必须释放，否则关机时会泄漏
    while (queue->count > 0) {
        Frame f = queue->frames[queue->head];
        frame_release(&f);
        queue->head = (queue->head + 1) % queue->capacity;
        queue->count--;
    }

    pthread_cond_destroy(&queue->empty);
    pthread_mutex_destroy(&queue->mutex);
    
    if (queue->frames) {
        free(queue->frames);
        queue->frames = NULL;
    }
    
    queue->capacity = 0;
    return 0;
}

int frame_queue_abort(FrameQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->abort = true;
    pthread_cond_broadcast(&queue->empty);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}