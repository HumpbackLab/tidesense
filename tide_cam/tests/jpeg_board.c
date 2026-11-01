#define _POSIX_C_SOURCE 200809L
#include "tide_cam.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double milliseconds(clockid_t clock)
{
    struct timespec t; clock_gettime(clock, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec * 1e-6;
}
static uint64_t checksum(const tide_cam_jpeg_pair *pair)
{
    uint64_t sum = 0;
    for (unsigned p = 0; p < 2; ++p) for (uint32_t i = 0; i < pair->frames[p].size; ++i)
        sum = sum * 33 + pair->frames[p].data[i];
    return sum;
}
int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "Usage: %s EXISTING_OUTPUT_DIRECTORY\n", argv[0]); return 2; }
    tide_cam_config config; tide_cam_config_init(&config); config.jpeg_quality = 80;
    tide_cam *c = NULL; const tide_cam_jpeg_pair *jpeg = NULL; const tide_cam_frame_set *raw = NULL;
    char error[256]; int status = tide_cam_open(&config, &c, error, sizeof(error));
    if (status) { fprintf(stderr, "open: %s\n", error); return 1; }
    double elapsed = 0, cpu = 0;
    for (unsigned n = 0; n < 20; ++n) {
        status = tide_cam_acquire(c, &raw, 5000);
        if (status) break;
        double start = milliseconds(CLOCK_MONOTONIC), start_cpu = milliseconds(CLOCK_THREAD_CPUTIME_ID);
        status = tide_cam_get_jpeg(c, &jpeg);
        elapsed += milliseconds(CLOCK_MONOTONIC) - start;
        cpu += milliseconds(CLOCK_THREAD_CPUTIME_ID) - start_cpu;
        if (status) break;
        if (jpeg->sequence != raw->sequence) { status = TIDE_CAM_DISCONTINUITY; break; }
        for (unsigned p = 0; p < 2; ++p) {
            if (jpeg->frames[p].timestamp_ns != raw->frames[p].timestamp_ns) { status = TIDE_CAM_DISCONTINUITY; break; }
            if (!n) {
                char path[512]; snprintf(path, sizeof(path), "%s/cam%u.jpg", argv[1], p);
                FILE *f = fopen(path, "wb");
                if (!f) { status = TIDE_CAM_IO; break; }
                int failed = fwrite(jpeg->frames[p].data, 1, jpeg->frames[p].size, f) != jpeg->frames[p].size;
                if (fclose(f)) failed = 1;
                if (failed) { status = TIDE_CAM_IO; break; }
                snprintf(path, sizeof(path), "%s/cam%u.pgm", argv[1], p); f = fopen(path, "wb");
                if (!f) { status = TIDE_CAM_IO; break; }
                fprintf(f, "P5\n%u %u\n255\n", raw->frames[p].width, raw->frames[p].height);
                failed = fwrite(raw->frames[p].data, 1, raw->frames[p].data_size, f) != raw->frames[p].data_size;
                if (fclose(f)) failed = 1;
                if (failed) { status = TIDE_CAM_IO; break; }
            }
        }
        if (status) break;
        printf("pair=%" PRIu64 " left=%u right=%u\n", jpeg->sequence, jpeg->frames[0].size, jpeg->frames[1].size);
        if (n == 19) {
            uint64_t sum = checksum(jpeg);
            status = tide_cam_stop(c);
            if (!status && checksum(jpeg) != sum) status = TIDE_CAM_IO;
        }
        int ret = tide_cam_release_jpeg(c, jpeg); jpeg = NULL;
        if (!status) status = ret;
        ret = tide_cam_release(c, raw); raw = NULL;
        if (!status) status = ret;
        if (status) break;
    }
    if (status) { tide_cam_get_error(c, error, sizeof(error)); fprintf(stderr, "JPEG test: %s (%d) %s\n", tide_cam_status_string(status), status, error); }
    if (jpeg) tide_cam_release_jpeg(c, jpeg);
    if (raw) tide_cam_release(c, raw);
    int cleanup = tide_cam_close(c);
    if (status || cleanup) return 1;
    printf("JPEG BOARD PASS: pairs=20 encode_pair_mean_ms=%.3f encode_pair_cpu_ms=%.3f borrowed_output_survives_stop=yes\n", elapsed/20, cpu/20);
    return 0;
}
