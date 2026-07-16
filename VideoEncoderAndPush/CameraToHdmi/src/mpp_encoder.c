#include "../include/mpp_encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mpp_encoder_init(EncoderContext *ctx, uint32_t width, uint32_t height, uint32_t fps)
{
    memset(ctx, 0, sizeof(EncoderContext));
    ctx->width = width;
    ctx->height = height;
    ctx->fps = fps;
    // 4K H.264 推荐码率，例如 10~20Mbps。这里设为 10Mbps
    ctx->bps = 10 * 1024 * 1024; 

    ctx->hor_stride = MPP_ALIGN(width, 16);
    ctx->ver_stride = MPP_ALIGN(height, 16);

    // 1. 创建 MPP 上下文和 API 接口
    if (mpp_create(&ctx->ctx, &ctx->mpi) != MPP_OK) {
        fprintf(stderr, "mpp_create failed\n");
        return -1;
    }

    // 2. 初始化为 H.264 编码器
    if (mpp_init(ctx->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC) != MPP_OK) {
        fprintf(stderr, "mpp_init failed\n");
        return -1;
    }

    // 3. 配置编码器参数
    MppEncCfg cfg;
    mpp_enc_cfg_init(&cfg);
    
    // 设置分辨率和格式 (假设输入严格为 NV12，对应 MPP_FMT_YUV420SP)
    mpp_enc_cfg_set_s32(cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", height);

    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", ctx->hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ctx->ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);

    // 设置码率控制 (CBR - 恒定码率)
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", ctx->bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", ctx->bps * 1.2);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", ctx->bps * 0.8);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", fps * 2); // 2秒一个I帧

    ctx->mpi->control(ctx->ctx, MPP_ENC_SET_CFG, cfg);
    mpp_enc_cfg_deinit(cfg);

    // 4. 获取 SPS/PPS 头 (H.264 解码必备)
    MppPacket packet = NULL;
    ctx->mpi->control(ctx->ctx, MPP_ENC_GET_EXTRA_INFO, &packet);
    if (packet) {
        ctx->header_size = mpp_packet_get_length(packet);
        ctx->header_data = malloc(ctx->header_size);
        memcpy(ctx->header_data, mpp_packet_get_pos(packet), ctx->header_size);
        // header_data 需要在推流或写入文件时放在最开头
        mpp_packet_deinit(&packet);
    }

    // 5. 创建用于导入 DMA FD 的 Buffer Group
    mpp_buffer_group_get_internal(&ctx->buf_grp, MPP_BUFFER_TYPE_EXT_DMA);

    return 0;
}

int mpp_encoder_encode(EncoderContext *ctx, const Frame *in_frame, EncodedPacket *out_packet)
{
    if (!ctx || !out_packet) return -1;

    MppBuffer mpp_buf = NULL;
    MppFrame mpp_frame = NULL;
    MppPacket mpp_packet = NULL;

    // 1. 将 DMA FD 导入为 MppBuffer
    MppBufferInfo info;
    memset(&info, 0, sizeof(info));
    info.type = MPP_BUFFER_TYPE_EXT_DMA;
    info.fd = in_frame->dma_fd;

    info.size = ctx->hor_stride * ctx->ver_stride * 3 / 2; 
    info.index = in_frame->index;

    // import_with_tag 实现了零拷贝，MPP 底层直接操作摄像头的物理内存
    mpp_buffer_import_with_tag(ctx->buf_grp, &info, &mpp_buf, "camera_in", __func__);

    // 2. 初始化并设置 MppFrame
    mpp_frame_init(&mpp_frame);
    mpp_frame_set_width(mpp_frame, in_frame->width);
    mpp_frame_set_height(mpp_frame, in_frame->height);
    mpp_frame_set_hor_stride(mpp_frame, in_frame->width);
    mpp_frame_set_ver_stride(mpp_frame, in_frame->height);
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

    // 清理送入帧的结构 (MPP 内部已持有引用)
    mpp_frame_deinit(&mpp_frame);
    if (mpp_buf) mpp_buffer_put(mpp_buf);

    // 4. 获取编码后的码流
    int retry_count = 0;
    const int max_retries = 100; // 最长等待约 200ms
    do {
        if (ctx->mpi->encode_get_packet(ctx->ctx, &mpp_packet) == MPP_OK && mpp_packet != NULL) {
            break; // 成功获取到包
        }
        usleep(2000); // 等待 2ms 再试
        retry_count++;
    } while (retry_count < max_retries);

    if (mpp_packet == NULL) {
        fprintf(stderr, "MPP get packet timeout\n");
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
// 处理完 EncodedPacket 后调用
void mpp_encoder_release_packet(EncodedPacket *packet)
{
    if (packet && packet->packet) {
        mpp_packet_deinit(&packet->packet);
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