#define _POSIX_C_SOURCE 200809L
#include "tide_cam.h"
#include "backend_mock.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static tide_cam *open_camera(void)
{
    mock_reset();
    tide_cam_config config;
    tide_cam_config_init(&config);
    config.jpeg_quality = 80;
    tide_cam *camera = NULL;
    char error[256];
    assert(!tide_cam_open(&config, &camera, error, sizeof(error)));
    return camera;
}
static void pause_ms(void) { struct timespec t = {0, 30000000}; nanosleep(&t, NULL); }

static void pairs_and_backpressure(void)
{
    tide_cam *c = open_camera();
    mock_push(3);
    const tide_cam_frame_set *raw;
    assert(!tide_cam_acquire(c, &raw, 1000) && raw->sequence == 1);
    uint64_t stamps[2] = {raw->frames[0].timestamp_ns, raw->frames[1].timestamp_ns};
    assert(!tide_cam_release(c, raw));
    pause_ms();
    tide_cam_stats stats;
    assert(!tide_cam_get_stats(c, &stats) && stats.captured == 1); /* Pending JPEG stalls capture. */
    const tide_cam_jpeg_pair *jpeg, *second;
    assert(!tide_cam_get_jpeg(c, &jpeg) && mock_jpeg_on_this_thread());
    assert(jpeg->sequence == 1);
    for (unsigned p = 0; p < 2; ++p) {
        const tide_cam_jpeg_frame *f = &jpeg->frames[p];
        assert(f->camera_id == p && f->timestamp_ns == stamps[p]);
        assert(f->width == 640 && f->height == 400 && f->size == 8);
        assert(f->data[0] == 0xff && f->data[2] == 1 && f->data[3] == p);
    }
    assert(tide_cam_get_jpeg(c, &second) == TIDE_CAM_BUSY && !second);
    assert(tide_cam_close(c) == TIDE_CAM_BUSY);
    pause_ms();
    assert(!tide_cam_get_stats(c, &stats) && stats.captured == 1); /* Borrow also stalls capture. */
    assert(!tide_cam_release_jpeg(c, jpeg));
    assert(tide_cam_release_jpeg(c, jpeg) == TIDE_CAM_INVALID);
    assert(!tide_cam_get_jpeg(c, &jpeg) && jpeg->sequence == 2);
    uint8_t saved[8]; memcpy(saved, jpeg->frames[0].data, sizeof(saved));
    assert(!tide_cam_stop(c));
    assert(!memcmp(saved, jpeg->frames[0].data, sizeof(saved))); /* stop cannot invalidate borrowed memory. */
    assert(tide_cam_close(c) == TIDE_CAM_BUSY);
    assert(!tide_cam_release_jpeg(c, jpeg));
    assert(tide_cam_get_jpeg(c, &jpeg) == TIDE_CAM_STOPPED && !jpeg);
    assert(!tide_cam_close(c) && !mock_active());
}
struct waiter { tide_cam *camera; int status; };
static void *wait_jpeg(void *arg)
{
    struct waiter *w = arg;
    const tide_cam_jpeg_pair *pair;
    w->status = tide_cam_get_jpeg(w->camera, &pair);
    return NULL;
}
static void stop_and_faults(void)
{
    tide_cam *c = open_camera();
    struct waiter w = {c, 0}; pthread_t thread;
    assert(!pthread_create(&thread, NULL, wait_jpeg, &w));
    pause_ms(); assert(!tide_cam_stop(c));
    assert(!pthread_join(thread, NULL) && w.status == TIDE_CAM_STOPPED);
    assert(!tide_cam_close(c));
    c = open_camera(); mock_push(1); mock_jpeg_fail(TIDE_CAM_IO);
    const tide_cam_jpeg_pair *pair;
    assert(tide_cam_get_jpeg(c, &pair) == TIDE_CAM_IO && !pair);
    assert(!tide_cam_stop(c));
    char error[256]; assert(!tide_cam_get_error(c, error, sizeof(error)) && strstr(error, "JPEG failure"));
    assert(!tide_cam_close(c));
    c = open_camera(); mock_push(1);
    const tide_cam_frame_set *raw;
    assert(!tide_cam_acquire(c, &raw, 1000)); assert(!tide_cam_release(c, raw));
    assert(!tide_cam_stop(c)); /* Cancel a pending pair when there is no JPEG consumer. */
    assert(!tide_cam_close(c));
}
static void *finish_encoding_after_stop(void *arg)
{
    tide_cam *c = arg;
    tide_cam_stats stats;
    do { assert(!tide_cam_get_stats(c, &stats)); if (!stats.terminal_status) pause_ms(); }
    while (!stats.terminal_status);
    mock_jpeg_hold(0);
    return NULL;
}
static void stop_during_encoding(void)
{
    tide_cam *c = open_camera(); mock_jpeg_hold(1); mock_push(1);
    struct waiter w = {c, 0}; pthread_t getter, controller;
    assert(!pthread_create(&getter, NULL, wait_jpeg, &w));
    mock_wait_jpeg_started();
    assert(tide_cam_close(c) == TIDE_CAM_BUSY);
    assert(!pthread_create(&controller, NULL, finish_encoding_after_stop, c));
    assert(!tide_cam_stop(c)); /* Must wait for the active encoder before stopping the backend. */
    assert(!pthread_join(getter, NULL) && w.status == TIDE_CAM_STOPPED);
    assert(!pthread_join(controller, NULL));
    assert(mock_jpeg_released() == 1); /* Stop discarded the completed output safely. */
    assert(!tide_cam_close(c));
}
int main(void)
{
    tide_cam_config config; tide_cam_config_init(&config);
    tide_cam *c; char error[256]; const tide_cam_jpeg_pair *pair;
    config.jpeg_quality = 100;
    assert(tide_cam_open(&config, &c, error, sizeof(error)) == TIDE_CAM_INVALID);
    config.jpeg_quality = 0; mock_reset();
    assert(!tide_cam_open(&config, &c, error, sizeof(error)));
    assert(tide_cam_get_jpeg(c, &pair) == TIDE_CAM_UNSUPPORTED && !pair);
    assert(!tide_cam_close(c));
    pairs_and_backpressure(); stop_and_faults(); stop_during_encoding();
    puts("JPEG: caller-thread encoding, pairing, backpressure, leases, stop and faults passed");
    return 0;
}
