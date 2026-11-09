/* Fixed-exposure/gain capture for black-balance, dark-column-FPN and flat-field
 * calibration.
 *
 * Uses the production tide_cam backend (same ISP profile, BLC tracking and
 * FSIN handling), then overrides AE with a fixed exposure and analog gain.
 * Saves per-frame column means plus first/last Y frames; fpn_analyze.py turns
 * dark captures into black/offset data and flat captures into column gains. */
#define _POSIX_C_SOURCE 200809L
#include "tide_cam.h"
#include "cvi_ae.h"
#include "cvi_isp.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int parse_u32(const char *value, uint32_t *out)
{
    if (!value || *value < '0' || *value > '9') return -1;
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno || *end || parsed > UINT32_MAX) return -1;
    *out = (uint32_t)parsed;
    return 0;
}

static int set_manual_ae(VI_PIPE pipe, uint32_t exposure_us, uint32_t gain,
                         char *error, size_t error_size)
{
    ISP_EXPOSURE_ATTR_S ae;
    int ret = CVI_ISP_GetExposureAttr(pipe, &ae);
    if (ret) {
        snprintf(error, error_size, "CVI_ISP_GetExposureAttr(%d)=%#x", pipe, ret);
        return -1;
    }
    ae.bByPass = CVI_FALSE;
    ae.enOpType = OP_TYPE_MANUAL;
    ae.stManual.enExpTimeOpType = OP_TYPE_MANUAL;
    ae.stManual.enAGainOpType = OP_TYPE_MANUAL;
    ae.stManual.enDGainOpType = OP_TYPE_MANUAL;
    ae.stManual.enISPDGainOpType = OP_TYPE_MANUAL;
    ae.stManual.u32ExpTime = exposure_us;
    ae.stManual.u32AGain = gain;
    ae.stManual.u32DGain = 1024;
    ae.stManual.u32ISPDGain = 1024;
    ret = CVI_ISP_SetExposureAttr(pipe, &ae);
    if (ret) {
        snprintf(error, error_size, "CVI_ISP_SetExposureAttr(%d)=%#x", pipe, ret);
        return -1;
    }
    return 0;
}

static int write_pgm(const char *path, const uint8_t *data, uint32_t width,
                     uint32_t height)
{
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    fprintf(file, "P5\n%u %u\n255\n", width, height);
    for (uint32_t y = 0; y < height; ++y) {
        if (fwrite(data + (size_t)y * width, 1, width, file) != width) {
            fclose(file);
            return -1;
        }
    }
    return fclose(file);
}

int main(int argc, char **argv)
{
    uint32_t frames = 80, skip = 8, exposure_us = 500, gain = 15872;
    const char *out_dir = ".";
    const char *mode = "dark";
    uint32_t width = 0, height = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            puts("tide_cam_fpn_calib [--mode dark|flat] [--frames N] [--skip N]"
                 " [--exposure-us U]"
                 " [--gain Q10] [--width W --height H] [--out DIR]\n"
                 "Hardware-synced fixed-exposure capture for the FPN LUT."
                 " Default: dark mode, 80 frames,"
                 " skip 8, 500 us, gain 15872 (15.5x), board mode, '.'\n"
                 "FPN correction is disabled for the capture.\n"
                 "dark: cover the lens. flat: illuminate a uniform, defocused"
                 " target and avoid clipping.\n"
                 "Run fpn_analyze.py on the output directory.");
            return 0;
        } else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            mode = argv[++i];
            if (strcmp(mode, "dark") && strcmp(mode, "flat")) {
                fputs("--mode must be dark or flat\n", stderr);
                return 2;
            }
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            if (parse_u32(argv[++i], &frames) || !frames) return 2;
        } else if (!strcmp(argv[i], "--skip") && i + 1 < argc) {
            if (parse_u32(argv[++i], &skip)) return 2;
        } else if (!strcmp(argv[i], "--exposure-us") && i + 1 < argc) {
            if (parse_u32(argv[++i], &exposure_us) || !exposure_us) return 2;
        } else if (!strcmp(argv[i], "--gain") && i + 1 < argc) {
            if (parse_u32(argv[++i], &gain) || gain < 1024) return 2;
        } else if (!strcmp(argv[i], "--width") && i + 1 < argc) {
            if (parse_u32(argv[++i], &width) || !width) return 2;
        } else if (!strcmp(argv[i], "--height") && i + 1 < argc) {
            if (parse_u32(argv[++i], &height) || !height) return 2;
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out_dir = argv[++i];
        } else {
            fprintf(stderr, "Unknown/incomplete option: %s\n", argv[i]);
            return 2;
        }
    }
    if ((width == 0) != (height == 0)) return 2;

    tide_cam_config config;
    tide_cam_config_init(&config);
    config.fpn = 0;
    if (width) config.mode = (tide_cam_mode){width, height, TIDE_CAM_MONO8};
    config.sync = TIDE_CAM_SYNC_HARDWARE;
    config.exposure_max_us = exposure_us;
    uint32_t w = config.mode.width, h = config.mode.height;

    tide_cam *camera = NULL;
    char error[256] = "";
    int status = tide_cam_open(&config, &camera, error, sizeof(error));
    if (status) {
        fprintf(stderr, "open: %s: %s\n", tide_cam_status_string(status), error);
        return 1;
    }
    int rc = 1;
    float *cols[2] = {0};
    uint8_t *first[2] = {0}, *last[2] = {0};
    uint64_t first_ts[2] = {0}, last_ts[2] = {0};
    uint64_t saturated[2] = {0};
    double level_min[2] = {1e9, 1e9}, level_max[2] = {0, 0};
    for (int p = 0; p < 2; ++p) {
        if (set_manual_ae((VI_PIPE)p, exposure_us, gain, error, sizeof(error))) {
            fprintf(stderr, "manual AE pipe %d: %s\n", p, error);
            goto out;
        }
    }
    for (int p = 0; p < 2; ++p) {
        ISP_EXP_INFO_S info;
        if (CVI_ISP_QueryExposureInfo((VI_PIPE)p, &info) == 0)
            printf("pipe %d requested exp=%u us again=%u; actual exp=%u us again=%u"
                   " dgain=%u ispd=%u\n", p, exposure_us, gain, info.u32ExpTime,
                   info.u32AGain, info.u32DGain, info.u32ISPDGain);
    }

    cols[0] = calloc((size_t)frames * w, sizeof(float));
    cols[1] = calloc((size_t)frames * w, sizeof(float));
    first[0] = malloc((size_t)w * h);
    first[1] = malloc((size_t)w * h);
    last[0] = malloc((size_t)w * h);
    last[1] = malloc((size_t)w * h);
    if (!cols[0] || !cols[1] || !first[0] || !first[1] || !last[0] || !last[1]) {
        fprintf(stderr, "out of memory\n");
        goto out;
    }

    uint32_t dropped = 0, collected = 0;
    while (collected < frames) {
        const tide_cam_frame_set *set;
        status = tide_cam_acquire(camera, &set, 2000);
        if (status == TIDE_CAM_TIMEOUT) continue;
        if (status) {
            tide_cam_get_error(camera, error, sizeof(error));
            fprintf(stderr, "acquire: %s: %s\n", tide_cam_status_string(status), error);
            goto out;
        }
        if (dropped < skip) {
            ++dropped;
            tide_cam_release(camera, set);
            continue;
        }
        for (uint32_t p = 0; p < set->camera_count; ++p) {
            const tide_cam_frame *frame = &set->frames[p];
            if (frame->camera_id > 1 || frame->width != w || frame->height != h) {
                fprintf(stderr, "unexpected frame %u: %ux%u camera=%u\n", p,
                        frame->width, frame->height, frame->camera_id);
                tide_cam_release(camera, set);
                goto out;
            }
            float *dst = cols[frame->camera_id] + (size_t)collected * w;
            for (uint32_t x = 0; x < w; ++x) {
                uint64_t sum = 0;
                for (uint32_t y = 0; y < h; ++y)
                    sum += frame->data[(size_t)y * frame->stride + x];
                dst[x] = (float)((double)sum / h);
            }
            uint8_t *first_dst = first[frame->camera_id];
            uint8_t *last_dst = last[frame->camera_id];
            uint64_t level_sum = 0;
            for (uint32_t y = 0; y < h; ++y) {
                const uint8_t *line = frame->data + (size_t)y * frame->stride;
                for (uint32_t x = 0; x < w; ++x) {
                    level_sum += line[x];
                    saturated[frame->camera_id] += line[x] == 255;
                }
            }
            double level = (double)level_sum / ((double)w * h);
            if (level < level_min[frame->camera_id])
                level_min[frame->camera_id] = level;
            if (level > level_max[frame->camera_id])
                level_max[frame->camera_id] = level;
            if (collected == 0) {
                first_ts[frame->camera_id] = frame->timestamp_ns;
                for (uint32_t y = 0; y < h; ++y)
                    memcpy(first_dst + (size_t)y * w,
                           frame->data + (size_t)y * frame->stride, w);
            }
            if (collected == frames - 1) {
                last_ts[frame->camera_id] = frame->timestamp_ns;
                for (uint32_t y = 0; y < h; ++y)
                    memcpy(last_dst + (size_t)y * w,
                           frame->data + (size_t)y * frame->stride, w);
            }
        }
        tide_cam_release(camera, set);
        ++collected;
    }

    for (int p = 0; p < 2; ++p) {
        const float *pc = cols[p];
        double global = 0;
        for (uint32_t n = 0; n < frames; ++n)
            for (uint32_t x = 0; x < w; ++x) global += pc[(size_t)n * w + x];
        global /= (double)frames * w;
        double pattern = 0, temporal = 0;
        for (uint32_t x = 0; x < w; ++x) {
            double mean = 0, var = 0;
            for (uint32_t n = 0; n < frames; ++n)
                mean += pc[(size_t)n * w + x];
            mean /= frames;
            for (uint32_t n = 0; n < frames; ++n) {
                double d = pc[(size_t)n * w + x] - mean;
                var += d * d;
            }
            pattern += (mean - global) * (mean - global);
            temporal += var / frames;
        }
        pattern = sqrt(pattern / w);
        temporal = sqrt(temporal / w);
        printf("pipe %d: level=%.3f pattern_std=%.3f temporal_std=%.3f"
               " saturated=%.3f%% first_ts=%llu last_ts=%llu\n", p, global,
               pattern, temporal,
               100.0 * saturated[p] / ((double)frames * w * h),
               (unsigned long long)first_ts[p], (unsigned long long)last_ts[p]);
        if (!strcmp(mode, "flat")) {
            double fraction = (double)saturated[p] / ((double)frames * w * h);
            if (fraction > 0.01)
                fprintf(stderr, "pipe %d: flat capture has %.2f%% clipped pixels;"
                        " reduce exposure\n", p, fraction * 100.0);
            if (global < 16.0)
                fprintf(stderr, "pipe %d: flat capture level is only %.2f LSB;"
                        " increase exposure or illumination\n", p, global);
        }
    }

    char path[512];
    for (int p = 0; p < 2; ++p) {
        snprintf(path, sizeof(path), "%s/cam%d_cols.f32", out_dir, p);
        FILE *file = fopen(path, "wb");
        if (!file || fwrite(cols[p], sizeof(float), (size_t)frames * w, file) !=
                         (size_t)frames * w || fclose(file)) {
            fprintf(stderr, "write %s failed\n", path);
            goto out;
        }
        snprintf(path, sizeof(path), "%s/cam%d_first.pgm", out_dir, p);
        if (write_pgm(path, first[p], w, h)) {
            fprintf(stderr, "write %s failed\n", path);
            goto out;
        }
        snprintf(path, sizeof(path), "%s/cam%d_last.pgm", out_dir, p);
        if (write_pgm(path, last[p], w, h)) {
            fprintf(stderr, "write %s failed\n", path);
            goto out;
        }
    }
    snprintf(path, sizeof(path), "%s/meta.txt", out_dir);
    FILE *meta = fopen(path, "w");
    if (!meta) {
        fprintf(stderr, "write %s failed\n", path);
        goto out;
    }
    fprintf(meta, "mode=%s\nwidth=%u\nheight=%u\nframes=%u\nskip=%u\n"
                  "exposure_us=%u\ngain_q10=%u\n", mode, w, h, frames, skip,
                  exposure_us, gain);
    for (int p = 0; p < 2; ++p) {
        ISP_EXP_INFO_S info;
        if (CVI_ISP_QueryExposureInfo((VI_PIPE)p, &info) == 0)
            fprintf(meta, "cam%d_actual_exposure_us=%u\ncam%d_actual_again=%u\n"
                          "cam%d_actual_dgain=%u\ncam%d_actual_ispdgain=%u\n",
                    p, info.u32ExpTime, p, info.u32AGain, p, info.u32DGain,
                    p, info.u32ISPDGain);
        fprintf(meta, "cam%d_first_ts=%llu\ncam%d_last_ts=%llu\n", p,
                (unsigned long long)first_ts[p], p,
                (unsigned long long)last_ts[p]);
        fprintf(meta, "cam%d_saturated_fraction=%.9f\n"
                      "cam%d_level_min=%.6f\ncam%d_level_max=%.6f\n",
                p, (double)saturated[p] / ((double)frames * w * h),
                p, level_min[p], p, level_max[p]);
    }
    if (fclose(meta)) {
        fprintf(stderr, "write %s failed\n", path);
        goto out;
    }
    printf("saved %u %s frames to %s\n", frames, mode, out_dir);
    rc = 0;
out:
    free(cols[0]);
    free(cols[1]);
    free(first[0]);
    free(first[1]);
    free(last[0]);
    free(last[1]);
    int stopped = tide_cam_stop(camera);
    int closed = tide_cam_close(camera);
    if (!rc && (stopped || closed)) rc = 1;
    return rc;
}
