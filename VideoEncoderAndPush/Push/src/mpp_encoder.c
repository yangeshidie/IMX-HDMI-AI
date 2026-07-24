#include "../include/mpp_encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int mpp_encoder_init(EncoderContext *ctx, uint32_t width, uint32_t height, uint32_t fps)
{
    memset(ctx, 0, sizeof(EncoderContext));
    ctx->width = width;
    ctx->height = height;
    ctx->fps = fps;
    ctx->bps = 10 * 1024 * 1024; // 10Mbps

    ctx->hor_stride = MPP_ALIGN(width, 16);
    ctx->ver_stride = MPP_ALIGN(height, 16);

    // 1. 创建 MPP 上下文和 API 接口
    if (mpp_create(&ctx->ctx, &ctx->mpi) != MPP_OK) {
        fprintf(stderr, "mpp_create failed\n");
        return -1;
    }

    // 2. 设置输出为阻塞模式 (重要！省去繁琐的 sleep 重试)
    MppPollType timeout = MPP_POLL_BLOCK;
    if (ctx->mpi->control(ctx->ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout) != MPP_OK) {
        fprintf(stderr, "Failed to set output timeout\n");
        return -1;
    }

    // 3. 初始化为 H.264 编码器
    if (mpp_init(ctx->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC) != MPP_OK) {
        fprintf(stderr, "mpp_init failed\n");
        return -1;
    }

    // 4. 配置编码器参数
    MppEncCfg cfg;
    mpp_enc_cfg_init(&cfg);
    
    mpp_enc_cfg_set_s32(cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", ctx->hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ctx->ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);

    // 设置码率控制 (CBR)
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", ctx->bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", ctx->bps * 1.2);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", ctx->bps * 0.8);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom", 1);    
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1);   
    mpp_enc_cfg_set_s32(cfg, "rc:gop", fps * 2);

    if (ctx->mpi->control(ctx->ctx, MPP_ENC_SET_CFG, cfg) != MPP_OK) {
        fprintf(stderr, "MPP_ENC_SET_CFG failed\n");
    }
    mpp_enc_cfg_deinit(cfg);

    // 5. 获取 SPS/PPS 头
    MppPacket packet = NULL;
    // 必须自己提供一块 buffer 给 MPP 写入头部信息，头部信息一般不超过 1024 字节
    char hdr_buf[1024]; 
    mpp_packet_init(&packet, hdr_buf, sizeof(hdr_buf));
    mpp_packet_set_length(packet, 0); // 重要：必须清零长度

    if (ctx->mpi->control(ctx->ctx, MPP_ENC_GET_HDR_SYNC, packet) == MPP_OK) {
        ctx->header_size = mpp_packet_get_length(packet);
        ctx->header_data = malloc(ctx->header_size);
        memcpy(ctx->header_data, mpp_packet_get_pos(packet), ctx->header_size);
    } else {
        fprintf(stderr, "Failed to get SPS/PPS header\n");
    }
    mpp_packet_deinit(&packet);

    mpp_buffer_group_get_external(&ctx->buf_grp, MPP_BUFFER_TYPE_DRM);

    return 0;
}

int mpp_encoder_encode(EncoderContext *ctx, const Frame *in_frame, EncodedPacket *out_packet)
{
    if (!ctx || !out_packet || !in_frame) return -1;

    MppBuffer mpp_buf = NULL;
    MppFrame mpp_frame = NULL;
    MppPacket mpp_packet = NULL;

    // 1. 将 DMA FD 导入为 MppBuffer
    MppBufferInfo info;
    memset(&info, 0, sizeof(info));
    info.type = MPP_BUFFER_TYPE_DRM; 
    info.fd = in_frame->dma_fd;
    info.size = in_frame->size;      
    info.index = in_frame->index;

    if (info.fd <= 0) {
        fprintf(stderr, "Error: Invalid DMA FD (%d)! Check your V4L2 export buffer logic.\n", info.fd);
        return -1;
    }

    if (mpp_buffer_import_with_tag(ctx->buf_grp, &info, &mpp_buf, "camera_in", __func__) != MPP_OK) {
        fprintf(stderr, "Failed to import DMA fd! fd: %d, req_size: %zu, index: %d\n", 
                info.fd, info.size, info.index);
        return -1;
    }

    // 2. 初始化并设置 MppFrame
    mpp_frame_init(&mpp_frame);
    mpp_frame_set_width(mpp_frame, in_frame->width);
    mpp_frame_set_height(mpp_frame, in_frame->height);
    mpp_frame_set_hor_stride(mpp_frame, ctx->hor_stride);
    mpp_frame_set_ver_stride(mpp_frame, ctx->ver_stride);
    mpp_frame_set_fmt(mpp_frame, MPP_FMT_YUV420SP);
    mpp_frame_set_pts(mpp_frame, in_frame->timestamp);
    mpp_frame_set_buffer(mpp_frame, mpp_buf);

    // 3. 送入编码器
    if (ctx->mpi->encode_put_frame(ctx->ctx, mpp_frame) != MPP_OK) {
        fprintf(stderr, "MPP put frame failed\n");
        mpp_frame_deinit(&mpp_frame);
        mpp_buffer_put(mpp_buf);
        return -1;
    }

    // 清理送入帧的结构 (MPP 底层已经增加了引用计数，这里必须释放表层结构)
    mpp_frame_deinit(&mpp_frame);
    if (mpp_buf) mpp_buffer_put(mpp_buf);

    // 4. 获取编码后的码流 (因为初始化设置了 BLOCK，这里只需调一次，无需死循环)
    if (ctx->mpi->encode_get_packet(ctx->ctx, &mpp_packet) != MPP_OK || mpp_packet == NULL) {
        fprintf(stderr, "MPP get packet failed\n");
        return -1;
    }

    // 5. 填充输出结构
    out_packet->data = mpp_packet_get_pos(mpp_packet);
    out_packet->length = mpp_packet_get_length(mpp_packet);
    out_packet->pts = mpp_packet_get_pts(mpp_packet);
    
    // 判断是否为关键帧 (I 帧)
    uint32_t meta = mpp_packet_get_flag(mpp_packet);
    out_packet->is_keyframe = (meta & MPP_PACKET_FLAG_INTRA) ? true : false;
    
    // 保存 packet 指针，外部处理完后需要释放
    out_packet->packet = mpp_packet; 

    return 0;
}

void mpp_encoder_release_packet(EncodedPacket *packet)
{
    if (packet && packet->packet) {
        mpp_packet_deinit((MppPacket*)&packet->packet);
        packet->packet = NULL;
    }
}

int mpp_encoder_deinit(EncoderContext *ctx)
{
    if (!ctx) return 0;
    if (ctx->mpi) {
        ctx->mpi->reset(ctx->ctx);
        mpp_destroy(ctx->ctx);
        ctx->mpi = NULL;
        ctx->ctx = NULL;
    }

    if (ctx->buf_grp) {
        mpp_buffer_group_put(ctx->buf_grp);
        ctx->buf_grp = NULL;
    }

    if (ctx->header_data) {
        free(ctx->header_data);
        ctx->header_data = NULL;
    }
    return 0;
}