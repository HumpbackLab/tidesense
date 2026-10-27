#define _POSIX_C_SOURCE 200809L
#include "tide_cam.h"
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t interrupted;
static void interrupt(int sig) { (void)sig; interrupted = 1; }

static int parse_rate(const char *value, tide_cam_rate *rate)
{
    if (*value < '0' || *value > '9') return -1;
    char *end;
    errno = 0;
    unsigned long num = strtoul(value, &end, 10), den = 1;
    if (errno || !num || num > UINT32_MAX) return -1;
    if (*end == '/') {
        value = end + 1;
        if (*value < '0' || *value > '9') return -1;
        errno = 0;
        den = strtoul(value, &end, 10);
        if (errno || !den || den > UINT32_MAX) return -1;
    }
    if (*end) return -1;
    *rate = (tide_cam_rate){(uint32_t)num, (uint32_t)den};
    return 0;
}

int main(int argc, char **argv)
{
    tide_cam_config config;
    tide_cam_config_init(&config);
    unsigned long limit = 100;
    tide_cam_rate requested_rate = {0, 0};
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            puts("tide_cam_capture [--frames N] [--width W] [--height H] [--fifo]\n"
                 "                 [--free-run [--fps N[/D]]]\n"
                 "Hardware sync (default): frame rate is determined by the trigger.\n"
                 "--fps is only valid with --free-run (default 20 fps).\n"
                 "N=0: continuous. No INI, helper process, ROS or VIO required.");
            return 0;
        } else if (!strcmp(argv[i], "--fifo")) config.queue = TIDE_CAM_FIFO;
        else if (!strcmp(argv[i], "--free-run")) config.sync = TIDE_CAM_SYNC_NONE;
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
            if (parse_rate(argv[++i], &requested_rate)) {
                fputs("--fps requires a positive integer or fraction, e.g. 30 or 30000/1001\n", stderr);
                return 2;
            }
        }
        else if ((!strcmp(argv[i], "--frames") || !strcmp(argv[i], "--width") ||
                  !strcmp(argv[i], "--height")) && i + 1 < argc) {
            char *end;
            const char *option = argv[i];
            const char *value = argv[++i];
            errno = 0;
            unsigned long number = strtoul(value, &end, 10);
            if (errno || *end || !*value || *value == '-') return 2;
            if (!strcmp(option, "--frames")) limit = number;
            else {
                if (!number || number > UINT32_MAX) return 2;
                if (!strcmp(option, "--width")) config.mode.width = (uint32_t)number;
                else config.mode.height = (uint32_t)number;
            }
        } else { fprintf(stderr, "Unknown/incomplete option: %s\n", argv[i]); return 2; }
    }
    if (config.sync == TIDE_CAM_SYNC_HARDWARE && requested_rate.num) {
        fputs("--fps requires --free-run; hardware-sync frame rate is determined by the trigger\n", stderr);
        return 2;
    }
    if (config.sync == TIDE_CAM_SYNC_NONE) {
        tide_cam_caps caps;
        if (tide_cam_query_caps(&caps)) return 1;
        config.free_run_fps = requested_rate.num ? requested_rate : caps.free_run_fps_default;
    }
    struct sigaction sa = {0};
    sa.sa_handler = interrupt;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    tide_cam *camera = NULL;
    char error[256] = "";
    int status = tide_cam_open(&config, &camera, error, sizeof(error));
    if (status) { fprintf(stderr, "open: %s: %s\n", tide_cam_status_string(status), error); return 1; }
    uint64_t first = 0, last = 0, count = 0, checksum = 0;
    while (!interrupted && (!limit || count < limit)) {
        const tide_cam_frame_set *frames;
        status = tide_cam_acquire(camera, &frames, 500);
        if (status == TIDE_CAM_TIMEOUT) { status = TIDE_CAM_OK; continue; }
        if (status) break;
        if (!count) first = frames->timestamp_ns;
        last = frames->timestamp_ns;
        /* Touch every pixel to exercise both image buffers in the example. */
        for (uint32_t p = 0; p < frames->camera_count; ++p)
            for (uint64_t i = 0; i < frames->frames[p].data_size; ++i)
                checksum += frames->frames[p].data[i];
        ++count;
        if (count == 1 || count % 20 == 0)
            printf("set=%" PRIu64 " cameras=%u size=%ux%u stride=%u bytes=%" PRIu64
                   " VI_ns=%" PRIu64 " right_VI_ns=%" PRIu64 "\n",
                   frames->sequence, frames->camera_count, frames->frames[0].width,
                   frames->frames[0].height, frames->frames[0].stride,
                   frames->frames[0].data_size, frames->timestamp_ns,
                   frames->camera_count > 1 ? frames->frames[1].timestamp_ns : 0);
        status = tide_cam_release(camera, frames);
        if (status) break;
    }
    if (status) {
        tide_cam_get_error(camera, error, sizeof(error));
        fprintf(stderr, "capture: %s: %s\n", tide_cam_status_string(status), error);
    }
    int stopped = tide_cam_stop(camera);
    tide_cam_stats stats;
    tide_cam_get_stats(camera, &stats);
    printf("frames=%" PRIu64 " hz=%.3f captured=%" PRIu64 " replaced=%" PRIu64 " checksum=%" PRIu64 "\n",
        count, last > first ? (count - 1) * 1e9 / (last - first) : 0,
        stats.captured, stats.replaced, checksum);
    int closed = tide_cam_close(camera);
    return status || stopped || closed ? 1 : 0;
}
