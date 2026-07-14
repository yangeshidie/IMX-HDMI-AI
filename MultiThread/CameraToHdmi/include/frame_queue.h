#include "common.h"
#include <pthread.h>
#include <stdbool.h>
#define FRAME_QUEUE_SIZE 8

typedef struct {
    Frame frames[FRAME_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t empty;
    pthread_cond_t full;
    bool abort;
} FrameQueue;

int frame_queue_init(FrameQueue *queue);
int frame_queue_push(FrameQueue *queue, const Frame *frame);
int frame_queue_pop(FrameQueue *queue, Frame *frame);
int frame_queue_deinit(FrameQueue *queue);
int frame_queue_abort(FrameQueue *queue);