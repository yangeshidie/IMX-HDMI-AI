#ifndef _MPP_ENCODER_H_
#define _MPP_ENCODER_H_

#ifndef MPP_ALIGN
#define MPP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))
#endif

#include <stdint.h>
#include <stdbool.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_packet.h> 
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_err.h>

#include "common.h" 

#ifndef MPP_PACKET_FLAG_INTRA
#define MPP_PACKET_FLAG_INTRA  (0x00000008)
#endif

// 编码输出的 H.264 数据包
typedef struct {
    uint8_t *data;      // 数据指针 (MPP 内部管理的内存)
    size_t length;      // 数据长度
    bool is_keyframe;   // 是否为 I 帧
    uint64_t pts;       // 时间戳
    MppPacket packet;   // 原始的 MPP packet，处理完后需要释放
} EncodedPacket;

// MPP 编码器上下文
typedef struct {
    MppCtx ctx;
    MppApi *mpi;
    MppBufferGroup buf_grp;
    
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t bps; // 比特率

    // 保存 SPS/PPS 头部数据 (H.264 需要放在最前面)
    uint8_t *header_data;
    size_t header_size;
    uint32_t hor_stride;
    uint32_t ver_stride;
} EncoderContext;

int mpp_encoder_init(EncoderContext *ctx, uint32_t width, uint32_t height, uint32_t fps);
int mpp_encoder_encode(EncoderContext *ctx, const Frame *in_frame, EncodedPacket *out_packet);
void mpp_encoder_release_packet(EncodedPacket *packet);
int mpp_encoder_deinit(EncoderContext *ctx);

#endif // _MPP_ENCODER_H_