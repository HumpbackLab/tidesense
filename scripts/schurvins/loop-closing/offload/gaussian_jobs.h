/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct gaussian_level {
  uint32_t width,height,input_stride,output_stride,input_offset,output_offset;
};
int schur_gaussian_make_job(uint64_t physical,const struct gaussian_level* levels,
                           unsigned count,uint32_t weights_offset,
                           uint8_t** commands,uint32_t* command_bytes);
#ifdef __cplusplus
}
#endif
