#define _POSIX_C_SOURCE 200809L
#include "backend.h"
#include "backend_mock.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct tide_cam_backend { uint64_t sequence; tide_cam_config config; };
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static unsigned tokens;
static int active, failure;

void mock_reset(void)
{
    pthread_mutex_lock(&lock);
    tokens = 0;
    failure = 0;
    pthread_mutex_unlock(&lock);
}
void mock_push(unsigned count)
{
    pthread_mutex_lock(&lock);
    tokens += count;
    pthread_cond_broadcast(&ready);
    pthread_mutex_unlock(&lock);
}
void mock_fail(int status)
{
    pthread_mutex_lock(&lock);
    failure = status;
    pthread_cond_broadcast(&ready);
    pthread_mutex_unlock(&lock);
}
int mock_active(void)
{
    pthread_mutex_lock(&lock);
    int result = active;
    pthread_mutex_unlock(&lock);
    return result;
}
int tide_backend_caps(tide_cam_caps *caps)
{
    static const tide_cam_mode modes[] = {
        {640, 400, TIDE_CAM_MONO8},
        {640, 480, TIDE_CAM_MONO8},
        {1280, 800, TIDE_CAM_MONO8},
    };
    *caps = (tide_cam_caps){2, sizeof(modes) / sizeof(modes[0]), modes, 3,
        TIDE_CAM_CLOCK_MONOTONIC, TIDE_CAM_TIMESTAMP_VI_ARRIVAL,
        {20, 1}, {167, 100}, {60, 1}, {20, 1}};
    return 0;
}
int tide_backend_open(const tide_cam_config *config, tide_cam_backend **out, char *error)
{
    *out = NULL;
    pthread_mutex_lock(&lock);
    if (active) {
        pthread_mutex_unlock(&lock);
        snprintf(error, TIDE_CAM_ERROR_SIZE, "Mock camera is busy");
        return TIDE_CAM_BUSY;
    }
    tide_cam_backend *b = calloc(1, sizeof(*b));
    if (!b) { pthread_mutex_unlock(&lock); return TIDE_CAM_NO_MEMORY; }
    active = 1;
    pthread_mutex_unlock(&lock);
    b->config = *config;
    *out = b;
    return 0;
}
int tide_backend_read(tide_cam_backend *b, tide_cam_frame_set *out, uint8_t *pixels, char *error)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 20000000;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    pthread_mutex_lock(&lock);
    while (!tokens && !failure) {
        if (pthread_cond_timedwait(&ready, &lock, &deadline)) {
            pthread_mutex_unlock(&lock);
            return TIDE_CAM_TIMEOUT;
        }
    }
    int status = failure;
    if (status) snprintf(error, TIDE_CAM_ERROR_SIZE, "Injected backend failure");
    else --tokens;
    pthread_mutex_unlock(&lock);
    if (status) return status;
    memset(out, 0, sizeof(*out));
    out->sequence = ++b->sequence;
    tide_cam_rate rate = b->config.sync == TIDE_CAM_SYNC_HARDWARE ?
        (tide_cam_rate){20, 1} : b->config.free_run_fps;
    out->timestamp_ns = 1000000000ULL + b->sequence * (1000000000ULL * rate.den / rate.num);
    out->camera_count = 2;
    out->sync = b->config.sync;
    out->clock = TIDE_CAM_CLOCK_MONOTONIC;
    out->timestamp_kind = TIDE_CAM_TIMESTAMP_VI_ARRIVAL;
    for (uint32_t p = 0; p < 2; ++p) {
        uint32_t width = b->config.mode.width, height = b->config.mode.height;
        size_t size = (size_t)width * height;
        uint8_t *data = pixels + p * size;
        memset(data, (uint8_t)(b->sequence + p), size);
        out->frames[p] = (tide_cam_frame){p, width, height, width, TIDE_CAM_MONO8,
            data, size, out->timestamp_ns + p * 2600000,
            (uint32_t)b->sequence};
    }
    return 0;
}
int tide_backend_close(tide_cam_backend *b, char *error)
{
    (void)error;
    free(b);
    pthread_mutex_lock(&lock);
    active = 0;
    pthread_mutex_unlock(&lock);
    return 0;
}
