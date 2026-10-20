/* TideSense UVC v1. Wire integers are explicitly little-endian. */
#ifndef TIDESENSE_UVC_H
#define TIDESENSE_UVC_H
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>

#define TS_WIDTH 1280
#define TS_HEIGHT 400
#define TS_FRAME_MAX (512U * 1024U)
#define TS_META_HEADER 96U
#define TS_SAMPLE_SIZE 32U
#define TS_BATCH_MAX 1024U
#define TS_RING_SIZE 2048U
#define TS_META_MAX (TS_META_HEADER + TS_SAMPLE_SIZE * TS_BATCH_MAX)
#define TS_FLAG_VI_MONOTONIC 1U
#define TS_FLAG_IMU_GAP 2U
#define TS_FLAG_OVERFLOW 4U
#define TS_FLAG_PAIR_DROP 8U
#define TS_FLAG_DISCONTINUITY 16U

struct ts_sample {
 uint64_t seq, ns;
 int16_t axis[6];
 uint32_t flags;
};
struct ts_frame {
 uint64_t seq, left_ns, right_ns, pair_drops;
 uint32_t left_ref, right_ref;
 size_t size;
 uint8_t jpeg[TS_FRAME_MAX];
};
struct ts_imu {
 int fd, enabled, rate_changed, fsin_started;
 char sysfs[512];
 unsigned old_rate;
 double accel_scale, gyro_scale;
 pthread_t thread;
 int thread_started;
 pthread_mutex_t lock;
 struct ts_sample ring[TS_RING_SIZE];
 size_t head, count;
 uint64_t seq, last_ns, drops, gap_events;
 atomic_int quit, error;
};
struct ts_source {
 pthread_mutex_t lock;
 struct ts_frame latest;
 struct ts_imu imu;
 pthread_t thread;
 int thread_started;
 atomic_int quit, error;
 unsigned quality;
 const char *ini;
};
extern volatile sig_atomic_t ts_stopped;
uint64_t ts_now(void);
int ts_setting(const struct ts_imu *, const char *, const char *);
int ts_imu_open(struct ts_imu *);
void ts_imu_close(struct ts_imu *);
/* Peek is transactional: retire samples only after the output was accepted. */
size_t ts_imu_peek(struct ts_imu *, uint64_t, struct ts_sample *, size_t, uint64_t *);
void ts_imu_commit(struct ts_imu *, uint64_t);
size_t ts_metadata(uint8_t *, const struct ts_frame *, const struct ts_imu *,
                   const struct ts_sample *, size_t, uint64_t, uint64_t, unsigned);
size_t ts_jpeg_with_metadata(uint8_t *, size_t, const struct ts_frame *, const uint8_t *, size_t);
int ts_source_open(struct ts_source *);
void ts_source_close(struct ts_source *);
int ts_uvc_run(struct ts_source *, const char *, const char *, unsigned);
int ts_file_run(struct ts_source *, const char *, unsigned);
#endif
