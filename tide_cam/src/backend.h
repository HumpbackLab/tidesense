#ifndef TIDE_CAM_BACKEND_H
#define TIDE_CAM_BACKEND_H
#include "tide_cam.h"

#define TIDE_CAM_ERROR_SIZE 256
typedef struct tide_cam_backend tide_cam_backend;

/* Exactly one selected backend implements these symbols. It owns all SDK types,
 * board wiring, synchronization and timestamp conversion. read() copies into
 * caller-owned pixel buffers, then releases every SDK frame before returning.
 * read() must return within a bounded interval when no frames arrive. */
int tide_backend_caps(tide_cam_caps *caps);
int tide_backend_open(const tide_cam_config *config, tide_cam_backend **backend,
                      char *error);
int tide_backend_read(tide_cam_backend *backend, tide_cam_frame_set *frames,
                      uint8_t *pixels, char *error);
int tide_backend_close(tide_cam_backend *backend, char *error);
#endif
