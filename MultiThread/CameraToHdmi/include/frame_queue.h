#include "common.h"
#include <pthread.h>

#define FRAME_QUEUE_SIZE 8

typedef struct {
    Frame frames[FRAME_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t empty;
    pthread_cond_t full;
} FrameQueue;

int frame_queue_init(FrameQueue *queue);
int frame_queue_push(FrameQueue *queue, const Frame *frame);
int frame_queue_pop(FrameQueue *queue, Frame *frame);
int frame_queue_destroy(FrameQueue *queue);