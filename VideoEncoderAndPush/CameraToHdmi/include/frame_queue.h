#ifndef _FRAME_QUEUE_H_
#define _FRAME_QUEUE_H_

#include "common.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct {
    Frame *frames;       // 动态数组，根据 capacity 分配
    int capacity;        // 队列最大容量
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t empty; // 仅保留 empty，供消费者死等
    bool abort;
} FrameQueue;

// 增加 capacity 参数
int frame_queue_init(FrameQueue *queue, int capacity);
int frame_queue_push(FrameQueue *queue, const Frame *frame);
int frame_queue_pop(FrameQueue *queue, Frame *frame);
int frame_queue_deinit(FrameQueue *queue);
int frame_queue_abort(FrameQueue *queue);

#endif // _FRAME_QUEUE_H_