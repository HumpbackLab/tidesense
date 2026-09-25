#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "cvi_sys.h"
#include "lk_shared.h"

#define LK_IOCTL_RUN _IOW('k', 1, uint32_t)

struct candidate { float score, x, y; };

static uint64_t clock_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void read_pgm(const char *path, uint8_t *dst)
{
    FILE *f = fopen(path, "rb");
    char magic[3];
    int w, h, maxv;
    if (!f || fscanf(f, "%2s%d%d%d", magic, &w, &h, &maxv) != 4 ||
        strcmp(magic, "P5") || w != 640 || h != 400 || maxv != 255 ||
        fgetc(f) != '\n' || fread(dst, 1, LK_PROBE_IMAGE_BYTES, f) != LK_PROBE_IMAGE_BYTES) {
        fprintf(stderr, "bad PGM: %s\n", path);
        exit(1);
    }
    fclose(f);
}

static int cmp_candidate(const void *a, const void *b)
{
    float x = ((const struct candidate *)a)->score;
    float y = ((const struct candidate *)b)->score;
    return (x < y) - (x > y);
}

static void choose_points(struct lk_probe_control *ctl, const uint8_t *image)
{
    struct candidate *c = calloc(4000, sizeof(*c));
    unsigned int n = 0, x, y, i;
    if (!c) exit(1);
    for (y = 24; y < 376; y += 8)
        for (x = 24; x < 616; x += 8) {
            float gx = (float)image[y * 640 + x + 1] - image[y * 640 + x - 1];
            float gy = (float)image[(y + 1) * 640 + x] - image[(y - 1) * 640 + x];
            c[n++] = (struct candidate){gx * gx + gy * gy, (float)x, (float)y};
        }
    qsort(c, n, sizeof(*c), cmp_candidate);
    ctl->count = 0;
    for (i = 0; i < n && ctl->count < LK_PROBE_POINTS; ++i) {
        unsigned int j;
        for (j = 0; j < ctl->count; ++j) {
            float dx = c[i].x - ctl->input[j].x;
            float dy = c[i].y - ctl->input[j].y;
            if (dx * dx + dy * dy < 144.0f) break;
        }
        if (j == ctl->count)
            ctl->input[ctl->count++] = (struct lk_probe_point){c[i].x, c[i].y};
    }
    free(c);
}

int main(int argc, char **argv)
{
    void *local = NULL, *shared = NULL;
    struct lk_probe_control remote_result;
    CVI_U64 phys = 0;
    struct lk_probe_control *ctl;
    int fd = -1, i, remote = 0;
    if (argc != 4 || (strcmp(argv[3], "local") && strcmp(argv[3], "remote"))) {
        fprintf(stderr, "usage: lk_probe_a53 prev.pgm next.pgm local|remote\n");
        return 2;
    }
    local = calloc(1, LK_PROBE_BUFFER_BYTES);
    if (!local) return 1;
    ctl = (struct lk_probe_control *)local;
    ctl->magic = LK_PROBE_MAGIC;
    read_pgm(argv[1], (uint8_t *)local + LK_PROBE_PREV_OFF);
    read_pgm(argv[2], (uint8_t *)local + LK_PROBE_NEXT_OFF);
    choose_points(ctl, (uint8_t *)local + LK_PROBE_PREV_OFF);
    printf("points=%u\n", ctl->count);
    remote = strcmp(argv[3], "remote") == 0;
    if (remote) {
        uint32_t p32;
        if (CVI_SYS_IonAlloc_Cached(&phys, &shared, "c906_lk_probe", LK_PROBE_BUFFER_BYTES) != CVI_SUCCESS ||
            !shared || phys > UINT32_MAX) {
            fprintf(stderr, "ION allocation failed\n");
            goto fail;
        }
        memcpy(shared, local, LK_PROBE_PYR_PREV_OFF);
        if (CVI_SYS_IonFlushCache(phys, shared, LK_PROBE_PYR_PREV_OFF) != CVI_SUCCESS) {
            fprintf(stderr, "ION flush failed\n");
            goto fail;
        }
        fd = open("/dev/c906-lk-probe", O_RDWR);
        if (fd < 0) { perror("open probe"); goto fail; }
        p32 = (uint32_t)phys;
        printf("physical=0x%08x bytes=%u\n", p32, LK_PROBE_BUFFER_BYTES);
        for (i = 0; i < 5; ++i) {
            uint64_t start = clock_ns();
            if (ioctl(fd, LK_IOCTL_RUN, &p32) < 0) { perror("C906 command"); goto fail; }
            if (CVI_SYS_IonInvalidateCache(phys, shared, LK_PROBE_CONTROL_BYTES) != CVI_SUCCESS) {
                fprintf(stderr, "ION invalidate failed\n");
                goto fail;
            }
            struct lk_probe_control *result = (struct lk_probe_control *)shared;
            printf("C906 run=%d status=%u valid=%u iterations=%u pyramid_ms=%.3f track_ms=%.3f total_ms=%.3f roundtrip_ms=%.3f\n",
                   i, result->status, result->valid, result->total_iterations,
                   result->pyramid_ticks / 25000.0, result->track_ticks / 25000.0,
                   result->total_ticks / 25000.0, (clock_ns() - start) / 1000000.0);
            fflush(stdout);
            if (result->status != 1) goto fail;
        }
        memcpy(&remote_result, shared, sizeof(remote_result));
    }
    for (i = 0; i < 5; ++i) {
        if (lk_probe_run(local, clock_ns) < 0) goto fail;
        printf("A53 run=%d valid=%u iterations=%u pyramid_ms=%.3f track_ms=%.3f total_ms=%.3f\n",
               i, ctl->valid, ctl->total_iterations, ctl->pyramid_ticks / 1000000.0,
               ctl->track_ticks / 1000000.0, ctl->total_ticks / 1000000.0);
        fflush(stdout);
    }
    if (remote) {
        unsigned int k, mask_differences = 0;
        float max_position_difference = 0.0f;
        for (k = 0; k < ctl->count; ++k) {
            float dx, dy, d;
            mask_differences += ctl->mask[k] != remote_result.mask[k];
            if (!ctl->mask[k] || !remote_result.mask[k]) continue;
            dx = ctl->output[k].x - remote_result.output[k].x;
            dy = ctl->output[k].y - remote_result.output[k].y;
            d = sqrtf(dx * dx + dy * dy);
            if (d > max_position_difference) max_position_difference = d;
        }
        printf("compare mask_differences=%u max_position_difference_px=%.6f\n",
               mask_differences, max_position_difference);
    }
    if (fd >= 0) close(fd);
    if (shared) CVI_SYS_IonFree(phys, shared);
    free(local);
    return 0;
fail:
    if (fd >= 0) close(fd);
    if (shared) CVI_SYS_IonFree(phys, shared);
    free(local);
    return 1;
}
