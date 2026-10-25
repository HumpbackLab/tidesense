#define _POSIX_C_SOURCE 200809L
#include "tide_cam.h"
#include <assert.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const tide_cam_frame_set *next(tide_cam *camera)
{
    const tide_cam_frame_set *frames = NULL;
    int status = tide_cam_acquire(camera, &frames, 5000);
    if (status) {
        char error[256];
        tide_cam_get_error(camera, error, sizeof(error));
        fprintf(stderr, "%s: %s\n", tide_cam_status_string(status), error);
    }
    assert(status == TIDE_CAM_OK && frames);
    return frames;
}

int main(void)
{
    struct sigaction before_int, before_term, after;
    assert(!sigaction(SIGINT, NULL, &before_int));
    assert(!sigaction(SIGTERM, NULL, &before_term));
    tide_cam_caps caps;
    assert(!tide_cam_query_caps(&caps));
    for (uint32_t mode = 0; mode < caps.mode_count; ++mode) for (int run = 0; run < 2; ++run) {
        tide_cam_config config;
        tide_cam_config_init(&config);
        config.mode = caps.modes[mode];
        config.queue = run ? TIDE_CAM_FIFO : TIDE_CAM_LATEST;
        tide_cam *camera = NULL, *busy = NULL;
        char error[256];
        int status = tide_cam_open(&config, &camera, error, sizeof(error));
        if (status) fprintf(stderr, "open: %s\n", error);
        assert(!status);
        assert(tide_cam_open(&config, &busy, error, sizeof(error)) == TIDE_CAM_BUSY && !busy);
        const tide_cam_frame_set *held = next(camera);
        for (int p = 0; p < 2; ++p) {
            assert(held->frames[p].width == config.mode.width);
            assert(held->frames[p].height == config.mode.height);
            assert(held->frames[p].stride == config.mode.width);
            assert(held->frames[p].data_size == (uint64_t)config.mode.width * config.mode.height);
        }
        assert(held->camera_count == 2 && held->sync == TIDE_CAM_SYNC_HARDWARE);
        assert(held->clock == TIDE_CAM_CLOCK_MONOTONIC && held->timestamp_kind == TIDE_CAM_TIMESTAMP_VI_ARRIVAL);
        assert(held->timestamp_ns == held->frames[0].timestamp_ns);
        size_t size = (size_t)held->frames[0].data_size;
        uint8_t *snapshot = malloc(2 * size);
        assert(snapshot);
        memcpy(snapshot, held->frames[0].data, size);
        memcpy(snapshot + size, held->frames[1].data, size);
        uint64_t previous[2] = {held->frames[0].timestamp_ns, held->frames[1].timestamp_ns};
        uint64_t start = held->timestamp_ns, end = start;
        for (int i = 0; i < 20; ++i) {
            const tide_cam_frame_set *frames = next(camera);
            for (int p = 0; p < 2; ++p) {
                assert(frames->frames[p].timestamp_ns > previous[p]);
                previous[p] = frames->frames[p].timestamp_ns;
                assert(!memcmp(snapshot + p * size, held->frames[p].data, size));
            }
            end = frames->timestamp_ns;
            assert(!tide_cam_release(camera, frames));
        }
        assert(tide_cam_close(camera) == TIDE_CAM_BUSY);
        assert(!tide_cam_stop(camera));
        assert(!memcmp(snapshot, held->frames[0].data, size));
        assert(!memcmp(snapshot + size, held->frames[1].data, size));
        assert(!tide_cam_release(camera, held));
        assert(tide_cam_release(camera, held) == TIDE_CAM_INVALID);
        free(snapshot);
        tide_cam_stats stats;
        assert(!tide_cam_get_stats(camera, &stats) && stats.acquired == 21 && !stats.leased);
        assert(!tide_cam_close(camera));
        assert(!sigaction(SIGINT, NULL, &after) && after.sa_handler == before_int.sa_handler);
        assert(!sigaction(SIGTERM, NULL, &after) && after.sa_handler == before_term.sa_handler);
        printf("mode=%ux%u run=%d queue=%s acquired=21 hz=%.3f replaced=%" PRIu64 " lifecycle=PASS\n",
               config.mode.width, config.mode.height, run, run ? "FIFO" : "LATEST",
               20e9 / (end - start), stats.replaced);
    }
    puts("tide_cam: real stereo, immutable leases, busy ownership, stop, signal handlers and in-process reopen PASS");
    return 0;
}
