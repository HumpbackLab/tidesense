#define _POSIX_C_SOURCE 200809L
#include "backend.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum slot_state { FREE, WRITING, READY, LEASED };
struct slot {
    tide_cam_frame_set frames;
    uint8_t *pixels;
    enum slot_state state;
};
struct tide_cam {
    tide_cam_config config;
    tide_cam_backend *backend;
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    pthread_t worker;
    struct slot *slots;
    uint32_t *queue, head, count;
    int stop_requested, terminal, joined, cleanup_status;
    tide_cam_stats stats;
    char error[TIDE_CAM_ERROR_SIZE];
};

static void copy_error(char *out, uint32_t size, const char *text)
{
    if (out && size) snprintf(out, size, "%s", text);
}

void tide_cam_config_init(tide_cam_config *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    tide_cam_caps caps;
    if (!tide_backend_caps(&caps) && caps.mode_count) {
        c->mode = caps.modes[0];
        c->sync = (caps.sync_modes & (1u << TIDE_CAM_SYNC_HARDWARE)) ?
            TIDE_CAM_SYNC_HARDWARE : TIDE_CAM_SYNC_NONE;
        if (c->sync == TIDE_CAM_SYNC_NONE) c->free_run_fps = caps.free_run_fps_default;
    }
    c->queue = TIDE_CAM_LATEST;
    c->buffer_count = 4;
    c->exposure_max_us = TIDE_CAM_DEFAULT_EXPOSURE_MAX_US;
}

void tide_cam_config_init_free_run(tide_cam_config *c)
{
    if (!c) return;
    tide_cam_config_init(c);
    tide_cam_caps caps;
    c->sync = TIDE_CAM_SYNC_NONE;
    if (!tide_backend_caps(&caps)) c->free_run_fps = caps.free_run_fps_default;
}

int tide_cam_query_caps(tide_cam_caps *caps)
{
    if (!caps) return TIDE_CAM_INVALID;
    return tide_backend_caps(caps);
}

static int validate(const tide_cam_config *c, const tide_cam_caps *caps)
{
    if (!c || c->buffer_count < 3 ||
        c->buffer_count > 64 || !c->mode.width || !c->mode.height ||
        !c->exposure_max_us ||
        (c->queue != TIDE_CAM_LATEST && c->queue != TIDE_CAM_FIFO) ||
        (c->sync != TIDE_CAM_SYNC_NONE && c->sync != TIDE_CAM_SYNC_HARDWARE))
        return TIDE_CAM_INVALID;
    if (!caps->camera_count || caps->camera_count > TIDE_CAM_MAX_CAMERAS)
        return TIDE_CAM_UNSUPPORTED;
    if (!(caps->sync_modes & (1u << c->sync))) return TIDE_CAM_UNSUPPORTED;
    if (c->sync == TIDE_CAM_SYNC_HARDWARE) {
        if (c->free_run_fps.num || c->free_run_fps.den) return TIDE_CAM_INVALID;
    } else {
        const tide_cam_rate *r = &c->free_run_fps;
        if (!r->num || !r->den) return TIDE_CAM_INVALID;
        if ((uint64_t)r->num * caps->free_run_fps_min.den <
            (uint64_t)caps->free_run_fps_min.num * r->den ||
            (uint64_t)r->num * caps->free_run_fps_max.den >
            (uint64_t)caps->free_run_fps_max.num * r->den)
            return TIDE_CAM_UNSUPPORTED;
    }
    for (uint32_t i = 0; i < caps->mode_count; ++i) {
        const tide_cam_mode *m = &caps->modes[i];
        if (c->mode.width == m->width && c->mode.height == m->height &&
            c->mode.pixel_format == m->pixel_format)
            return TIDE_CAM_OK;
    }
    return TIDE_CAM_UNSUPPORTED;
}

static void *capture(void *arg)
{
    tide_cam *c = arg;
    for (;;) {
        uint32_t index;
        char error[TIDE_CAM_ERROR_SIZE] = "";
        pthread_mutex_lock(&c->mutex);
        if (c->stop_requested) { pthread_mutex_unlock(&c->mutex); break; }
        for (index = 0; index < c->config.buffer_count; ++index)
            if (c->slots[index].state == FREE) break;
        if (index == c->config.buffer_count && c->config.queue == TIDE_CAM_LATEST && c->count) {
            index = c->queue[c->head];
            c->head = (c->head + 1) % c->config.buffer_count;
            --c->count;
            ++c->stats.replaced;
        }
        if (index == c->config.buffer_count) {
            c->terminal = TIDE_CAM_OVERFLOW;
            copy_error(c->error, sizeof(c->error), "Frame pool exhausted; release frames promptly or increase buffer_count");
            pthread_cond_broadcast(&c->ready);
            pthread_mutex_unlock(&c->mutex);
            break;
        }
        c->slots[index].state = WRITING;
        pthread_mutex_unlock(&c->mutex);
        int status = tide_backend_read(c->backend, &c->slots[index].frames,
                                       c->slots[index].pixels, error);
        pthread_mutex_lock(&c->mutex);
        if (c->stop_requested || status != TIDE_CAM_OK) {
            c->slots[index].state = FREE;
            if (!c->stop_requested && status != TIDE_CAM_TIMEOUT) {
                c->terminal = status;
                copy_error(c->error, sizeof(c->error), error);
            }
            int done = c->stop_requested || status != TIDE_CAM_TIMEOUT;
            pthread_cond_broadcast(&c->ready);
            pthread_mutex_unlock(&c->mutex);
            if (done) break;
            continue;
        }
        if (c->config.queue == TIDE_CAM_LATEST && c->count) {
            uint32_t old = c->queue[c->head];
            c->head = (c->head + 1) % c->config.buffer_count;
            --c->count;
            c->slots[old].state = FREE;
            ++c->stats.replaced;
        }
        c->slots[index].state = READY;
        c->queue[(c->head + c->count) % c->config.buffer_count] = index;
        ++c->count;
        ++c->stats.captured;
        pthread_cond_broadcast(&c->ready);
        pthread_mutex_unlock(&c->mutex);
    }
    /* On faults too, promptly stop FSIN/ISP instead of waiting for the caller. */
    char error[TIDE_CAM_ERROR_SIZE] = "";
    int status = tide_backend_close(c->backend, error);
    pthread_mutex_lock(&c->mutex);
    c->backend = NULL;
    c->cleanup_status = status;
    if (status && !c->terminal) {
        c->terminal = status;
        copy_error(c->error, sizeof(c->error), error);
    }
    if (!c->terminal) c->terminal = TIDE_CAM_STOPPED;
    pthread_cond_broadcast(&c->ready);
    pthread_mutex_unlock(&c->mutex);
    return NULL;
}

static void destroy(tide_cam *c)
{
    if (c->slots) for (uint32_t i = 0; i < c->config.buffer_count; ++i) free(c->slots[i].pixels);
    free(c->slots);
    free(c->queue);
    pthread_cond_destroy(&c->ready);
    pthread_mutex_destroy(&c->mutex);
    free(c);
}

int tide_cam_open(const tide_cam_config *config, tide_cam **camera, char *error, uint32_t error_size)
{
    tide_cam_caps caps;
    if (!camera) return TIDE_CAM_INVALID;
    *camera = NULL;
    copy_error(error, error_size, "");
    int status = tide_cam_query_caps(&caps);
    if (!status) status = validate(config, &caps);
    if (status) {
        const char *reason = tide_cam_status_string(status);
        if (config && config->sync == TIDE_CAM_SYNC_HARDWARE &&
            (config->free_run_fps.num || config->free_run_fps.den))
            reason = "Hardware-sync frame rate is determined by the trigger; free_run_fps must be {0,0}";
        copy_error(error, error_size, reason);
        return status;
    }
    tide_cam *c = calloc(1, sizeof(*c));
    if (!c) return TIDE_CAM_NO_MEMORY;
    c->config = *config;
    pthread_condattr_t attr;
    if (pthread_mutex_init(&c->mutex, NULL)) { free(c); return TIDE_CAM_IO; }
    if (pthread_condattr_init(&attr)) { pthread_mutex_destroy(&c->mutex); free(c); return TIDE_CAM_IO; }
    status = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!status) status = pthread_cond_init(&c->ready, &attr);
    pthread_condattr_destroy(&attr);
    if (status) { pthread_mutex_destroy(&c->mutex); free(c); return TIDE_CAM_IO; }
    c->slots = calloc(config->buffer_count, sizeof(*c->slots));
    c->queue = calloc(config->buffer_count, sizeof(*c->queue));
    uint64_t bytes = (uint64_t)config->mode.width * config->mode.height * caps.camera_count;
    if (!c->slots || !c->queue || bytes > SIZE_MAX) { destroy(c); return TIDE_CAM_NO_MEMORY; }
    for (uint32_t i = 0; i < config->buffer_count; ++i) {
        c->slots[i].pixels = malloc((size_t)bytes);
        if (!c->slots[i].pixels) { destroy(c); return TIDE_CAM_NO_MEMORY; }
    }
    status = tide_backend_open(config, &c->backend, c->error);
    if (!status && pthread_create(&c->worker, NULL, capture, c)) {
        char ignored[TIDE_CAM_ERROR_SIZE];
        tide_backend_close(c->backend, ignored);
        copy_error(c->error, sizeof(c->error), "Cannot start capture thread");
        status = TIDE_CAM_IO;
    }
    if (status) {
        copy_error(error, error_size, c->error);
        destroy(c);
        return status;
    }
    *camera = c;
    return TIDE_CAM_OK;
}

int tide_cam_acquire(tide_cam *c, const tide_cam_frame_set **frames, int timeout_ms)
{
    if (!c || !frames || timeout_ms < -1) return TIDE_CAM_INVALID;
    *frames = NULL;
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    if (timeout_ms > 0) {
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
        deadline.tv_sec += deadline.tv_nsec / 1000000000L;
        deadline.tv_nsec %= 1000000000L;
    }
    pthread_mutex_lock(&c->mutex);
    while (!c->count && !c->terminal && !c->stop_requested) {
        if (!timeout_ms) { pthread_mutex_unlock(&c->mutex); return TIDE_CAM_TIMEOUT; }
        int rc = timeout_ms < 0 ? pthread_cond_wait(&c->ready, &c->mutex) :
            pthread_cond_timedwait(&c->ready, &c->mutex, &deadline);
        if (rc) {
            if (rc == ETIMEDOUT && (c->count || c->terminal || c->stop_requested)) continue;
            pthread_mutex_unlock(&c->mutex);
            return rc == ETIMEDOUT ? TIDE_CAM_TIMEOUT : TIDE_CAM_IO;
        }
    }
    int status = TIDE_CAM_OK;
    if (c->count) {
        uint32_t index = c->queue[c->head];
        c->head = (c->head + 1) % c->config.buffer_count;
        --c->count;
        c->slots[index].state = LEASED;
        ++c->stats.acquired;
        ++c->stats.leased;
        *frames = &c->slots[index].frames;
    } else status = c->terminal ? c->terminal : TIDE_CAM_STOPPED;
    pthread_mutex_unlock(&c->mutex);
    return status;
}

int tide_cam_release(tide_cam *c, const tide_cam_frame_set *frames)
{
    if (!c || !frames) return TIDE_CAM_INVALID;
    int status = TIDE_CAM_INVALID;
    pthread_mutex_lock(&c->mutex);
    for (uint32_t i = 0; i < c->config.buffer_count; ++i) {
        if (&c->slots[i].frames == frames && c->slots[i].state == LEASED) {
            c->slots[i].state = FREE;
            --c->stats.leased;
            status = TIDE_CAM_OK;
            break;
        }
    }
    pthread_mutex_unlock(&c->mutex);
    return status;
}

int tide_cam_stop(tide_cam *c)
{
    if (!c) return TIDE_CAM_INVALID;
    pthread_mutex_lock(&c->mutex);
    c->stop_requested = 1;
    pthread_cond_broadcast(&c->ready);
    pthread_mutex_unlock(&c->mutex);
    if (!c->joined) { pthread_join(c->worker, NULL); c->joined = 1; }
    return c->cleanup_status;
}

int tide_cam_close(tide_cam *c)
{
    if (!c) return TIDE_CAM_OK;
    pthread_mutex_lock(&c->mutex);
    int busy = c->stats.leased != 0;
    pthread_mutex_unlock(&c->mutex);
    if (busy) return TIDE_CAM_BUSY;
    int status = tide_cam_stop(c);
    destroy(c);
    return status;
}

int tide_cam_get_stats(tide_cam *c, tide_cam_stats *stats)
{
    if (!c || !stats) return TIDE_CAM_INVALID;
    pthread_mutex_lock(&c->mutex);
    *stats = c->stats;
    stats->queued = c->count;
    stats->terminal_status = c->terminal;
    pthread_mutex_unlock(&c->mutex);
    return TIDE_CAM_OK;
}

int tide_cam_get_error(tide_cam *c, char *error, uint32_t size)
{
    if (!c || !error || !size) return TIDE_CAM_INVALID;
    pthread_mutex_lock(&c->mutex);
    copy_error(error, size, c->error);
    pthread_mutex_unlock(&c->mutex);
    return TIDE_CAM_OK;
}

const char *tide_cam_status_string(int status)
{
    switch (status) {
    case TIDE_CAM_OK: return "Success";
    case TIDE_CAM_INVALID: return "Invalid argument";
    case TIDE_CAM_UNSUPPORTED: return "Unsupported capture mode";
    case TIDE_CAM_BUSY: return "Camera or frame set is busy";
    case TIDE_CAM_IO: return "Capture I/O error";
    case TIDE_CAM_NO_MEMORY: return "Allocation failed";
    case TIDE_CAM_TIMEOUT: return "Timed out";
    case TIDE_CAM_STOPPED: return "Capture stopped";
    case TIDE_CAM_OVERFLOW: return "Capture queue overflow";
    case TIDE_CAM_DISCONTINUITY: return "Camera sequence or timestamp discontinuity";
    case TIDE_CAM_NOT_FOUND: return "Configuration file not found";
    default: return "Unknown error";
    }
}
