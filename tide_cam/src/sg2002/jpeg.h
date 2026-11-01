#ifndef TIDE_CAM_SG2002_JPEG_H
#define TIDE_CAM_SG2002_JPEG_H
#include "backend.h"
#include <linux/cvi_comm_video.h>

typedef struct tide_jpeg tide_jpeg;
int tide_jpeg_open(const tide_cam_config *config, tide_jpeg **out, char *error);
int tide_jpeg_encode(tide_jpeg *jpeg, const VIDEO_FRAME_INFO_S input[2],
                     tide_cam_jpeg_pair *pair, char *error);
int tide_jpeg_release(tide_jpeg *jpeg, char *error);
int tide_jpeg_abort(tide_jpeg *jpeg, char *error);
void tide_jpeg_close(tide_jpeg *jpeg);
#endif
