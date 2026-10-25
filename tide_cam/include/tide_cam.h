#ifndef TIDE_CAM_H
#define TIDE_CAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIDE_CAM_MAX_CAMERAS 4u

typedef struct tide_cam tide_cam;

typedef enum {
    TIDE_CAM_OK = 0,
    TIDE_CAM_INVALID = -1,
    TIDE_CAM_UNSUPPORTED = -2,
    TIDE_CAM_BUSY = -3,
    TIDE_CAM_IO = -4,
    TIDE_CAM_NO_MEMORY = -5,
    TIDE_CAM_TIMEOUT = -6,
    TIDE_CAM_STOPPED = -7,
    TIDE_CAM_OVERFLOW = -8,
    TIDE_CAM_DISCONTINUITY = -9
} tide_cam_status;

typedef enum { TIDE_CAM_MONO8 = 1 } tide_cam_pixel_format;
typedef enum { TIDE_CAM_SYNC_NONE = 0, TIDE_CAM_SYNC_HARDWARE = 1 } tide_cam_sync;
typedef enum { TIDE_CAM_LATEST = 0, TIDE_CAM_FIFO = 1 } tide_cam_queue;
typedef enum { TIDE_CAM_CLOCK_MONOTONIC = 1 } tide_cam_clock;
typedef enum { TIDE_CAM_TIMESTAMP_VI_ARRIVAL = 1 } tide_cam_timestamp_kind;

typedef struct {
    uint32_t width, height;
    tide_cam_pixel_format pixel_format;
} tide_cam_mode;

typedef struct {
    uint32_t num, den;
} tide_cam_rate;

typedef struct {
    uint32_t camera_count;
    uint32_t mode_count;
    const tide_cam_mode *modes; /* Static storage, valid for process lifetime. */
    uint32_t sync_modes;       /* Bit (1u << tide_cam_sync). */
    tide_cam_clock clock;
    tide_cam_timestamp_kind timestamp_kind;
    tide_cam_rate hardware_sync_fps; /* Read-only trigger cadence, never a setting. */
    tide_cam_rate free_run_fps_min, free_run_fps_max, free_run_fps_default;
} tide_cam_caps;

typedef struct {
    tide_cam_mode mode;
    tide_cam_sync sync;
    tide_cam_queue queue;
    uint32_t buffer_count; /* 3..64 frame sets, including outstanding leases. */
    uint32_t exposure_max_us;
    /* Only valid for SYNC_NONE. Must be {0,0} in hardware-sync mode. */
    tide_cam_rate free_run_fps;
} tide_cam_config;

typedef struct {
    uint32_t camera_id; /* Stable board order: TideSense 0=left, 1=right. */
    uint32_t width, height, stride;
    tide_cam_pixel_format pixel_format;
    const uint8_t *data;
    uint64_t data_size;
    uint64_t timestamp_ns; /* VI arrival PTS in CLOCK_MONOTONIC, not exposure. */
    uint32_t sensor_sequence; /* VI time_ref, wraps as uint32_t. */
} tide_cam_frame;

typedef struct {
    uint64_t sequence;
    uint64_t timestamp_ns; /* Camera 0 VI arrival time, no calibration shift. */
    uint32_t camera_count;
    tide_cam_sync sync; /* Configured synchronization, not an accuracy claim. */
    tide_cam_clock clock;
    tide_cam_timestamp_kind timestamp_kind;
    tide_cam_frame frames[TIDE_CAM_MAX_CAMERAS];
} tide_cam_frame_set;

typedef struct {
    uint64_t captured, acquired, replaced;
    uint32_t queued, leased;
    int terminal_status;
} tide_cam_stats;

void tide_cam_config_init(tide_cam_config *config);
/* Selects free-running capture and the backend's default free-run frame rate. */
void tide_cam_config_init_free_run(tide_cam_config *config);
int tide_cam_query_caps(tide_cam_caps *caps);
/* Error text is optional, copied to error_size bytes including NUL. */
int tide_cam_open(const tide_cam_config *config, tide_cam **camera,
                  char *error, uint32_t error_size);
/* timeout_ms: -1=infinite, 0=poll. A successful acquire must be released once.
 * Frame pointers/pixels remain valid until release, including after stop.
 * This is one stream with one logical consumer; outstanding leases are allowed.
 * Pending frames drain before a terminal error/STOPPED is returned. */
int tide_cam_acquire(tide_cam *camera, const tide_cam_frame_set **frames, int timeout_ms);
int tide_cam_release(tide_cam *camera, const tide_cam_frame_set *frames);
/* Stops/joins capture and releases hardware; wakes blocked acquire calls.
 * stop/close must be externally serialized; neither is signal-handler safe. */
int tide_cam_stop(tide_cam *camera);
/* Returns BUSY without stopping/freeing while any frame set is leased.
 * No other API call may run concurrently with close. NULL is accepted. */
int tide_cam_close(tide_cam *camera);
int tide_cam_get_stats(tide_cam *camera, tide_cam_stats *stats);
int tide_cam_get_error(tide_cam *camera, char *error, uint32_t error_size);
const char *tide_cam_status_string(int status);

#ifdef __cplusplus
}
#endif
#endif
