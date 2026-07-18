#ifndef _RGA_PROCESS_H_
#define _RGA_PROCESS_H_

#include "common.h"
#include <rga/RgaApi.h>
#include <rga/rga.h>


// src_frame 来自 Camera，dst_frame 来自 DRM
int rga_process_init();
int rga_process_convert_scale(const Frame *src_frame, const Frame *dst_frame);
void rga_process_deinit();

#endif