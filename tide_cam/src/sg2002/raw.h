#ifndef TIDE_CAM_RAW_H
#define TIDE_CAM_RAW_H

#include "sample_comm.h"

typedef struct tide_raw_capture tide_raw_capture;

int tide_raw_open(uint32_t width, uint32_t height, tide_raw_capture **out, char *error);
int tide_raw_read(VI_PIPE pipe, VIDEO_FRAME_INFO_S *frame, int timeout_ms);
int tide_raw_release(VI_PIPE pipe, const VIDEO_FRAME_INFO_S *frame);
int tide_raw_copy(const tide_raw_capture *raw, VI_PIPE pipe, const VIDEO_FRAME_S *frame,
                  uint8_t *pixels, char *error);
void tide_raw_stop(tide_raw_capture *raw);
void tide_raw_close(tide_raw_capture *raw);

#endif
