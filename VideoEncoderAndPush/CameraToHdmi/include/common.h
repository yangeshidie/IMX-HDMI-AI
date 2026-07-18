#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <stdatomic.h> 

#define         THREAD_OFF          (0)
#define         THREAD_ON           (1)

#define         THREAD_DISPLAY      THREAD_ON
#define         THREAD_ENCODER      THREAD_ON
#define         THREAD_AI           THREAD_OFF

#define         ACTIVE_THREAD_COUNT             (THREAD_DISPLAY + THREAD_ENCODER + THREAD_AI)



typedef struct {
    int dma_fd;             //该帧图像的fd
    int width;              //图像宽度          
    int height;             //高度
    int format;             //格式
    int index;              //摄像头buffer的索引，用于归还
    uint64_t timestamp;     //时间戳

    //atomic_int ref_count;   //引用计数
    void *camera_ctx;       //摄像头上下文，用于归还
} Frame;

void frame_retain(Frame *frame);
void frame_release(Frame *frame);

#endif // _COMMON_H_