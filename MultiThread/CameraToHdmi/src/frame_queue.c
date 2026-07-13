#include "../include/frame_queue.h"

int frame_queue_init(FrameQueue *queue)
{
    if (!queue) return -1;

    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;

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
int frame_queue_push(FrameQueue *queue, const Frame *frame);
int frame_queue_pop(FrameQueue *queue, Frame *frame);
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