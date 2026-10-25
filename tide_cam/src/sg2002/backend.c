#define _GNU_SOURCE
#include "backend.h"
#include "sample_comm.h"
#include "ov9281_isp.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct tide_cam_backend {
    tide_cam_config config;
    SAMPLE_VI_CONFIG_S vi;
    VIDEO_FRAME_INFO_S frames[2];
    int held[2], lock_fd, sys_ready, vi_ready, fsin_owned;
    uint64_t previous[2], delivered[2], sequence, progress_ns;
    uint32_t references[2], warmup, unmatched;
    uint64_t period_ns;
    int read_timeout_ms, rate_pending;
    char iio[PATH_MAX];
};

/* SAMPLE_COMM_VI_StartViChn sets depth before enabling the SDK channel.
 * CMake propagates --wrap to static-library consumers. No runtime helper. */
CVI_S32 __real_CVI_VI_SetChnAttr(VI_PIPE pipe, VI_CHN channel, VI_CHN_ATTR_S *attr);
CVI_S32 __wrap_CVI_VI_SetChnAttr(VI_PIPE pipe, VI_CHN channel, VI_CHN_ATTR_S *attr)
{
    VI_CHN_ATTR_S queued = *attr;
    queued.u32Depth = 3;
    return __real_CVI_VI_SetChnAttr(pipe, channel, &queued);
}

static uint64_t monotonic_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static int sdk_error(char *error, const char *operation, int status)
{
    snprintf(error, TIDE_CAM_ERROR_SIZE, "%s failed (%#x)", operation, (unsigned)status);
    return TIDE_CAM_IO;
}

static int read_attr(const char *dir, const char *name, long *value)
{
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int ok = fscanf(f, "%ld", value) == 1;
    fclose(f);
    return ok ? 0 : -1;
}

static int write_attr(const char *dir, const char *name, long value)
{
    char path[PATH_MAX], text[32];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return -1;
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    int size = snprintf(text, sizeof(text), "%ld\n", value);
    ssize_t written;
    do { written = write(fd, text, (size_t)size); } while (written < 0 && errno == EINTR);
    int rc = close(fd);
    return written == size && !rc ? 0 : -1;
}

static int find_fsin(char *path)
{
    glob_t paths;
    if (glob("/sys/bus/iio/devices/iio:device*", 0, NULL, &paths)) return -1;
    int result = -1;
    for (size_t i = 0; i < paths.gl_pathc; ++i) {
        char name_path[PATH_MAX], name[32], real[PATH_MAX];
        long enabled;
        if (snprintf(name_path, sizeof(name_path), "%s/name", paths.gl_pathv[i]) >= (int)sizeof(name_path)) continue;
        FILE *f = fopen(name_path, "r");
        if (!f) continue;
        int ok = fscanf(f, "%31s", name) == 1;
        fclose(f);
        if (ok && !strcmp(name, "sc7u22") && realpath(paths.gl_pathv[i], real) &&
            strstr(real, "/41a0000.spi2/") &&
            !read_attr(paths.gl_pathv[i], "fsin_enable", &enabled)) {
            snprintf(path, PATH_MAX, "%s", paths.gl_pathv[i]);
            result = 0;
            break;
        }
    }
    globfree(&paths);
    return result;
}

/* Same ownership protocol as the existing fsin.sh, implemented in-process. */
static int fsin(tide_cam_backend *b, int start, char *error)
{
    if (mkdir("/run/tidesense-fsin.lock", 0700)) {
        snprintf(error, TIDE_CAM_ERROR_SIZE, "FSIN control busy or /run is not writable");
        return TIDE_CAM_BUSY;
    }
    int status = TIDE_CAM_OK;
    long enabled, accel_rate, gyro_rate;
    if (start == 0) {
        if (read_attr(b->iio, "fsin_enable", &enabled)) status = TIDE_CAM_IO;
        else if (enabled) status = TIDE_CAM_BUSY;
        else if (read_attr(b->iio, "in_accel_sampling_frequency", &accel_rate) ||
                 read_attr(b->iio, "in_anglvel_sampling_frequency", &gyro_rate)) status = TIDE_CAM_IO;
        else if (accel_rate != 200 || gyro_rate != 200) status = TIDE_CAM_UNSUPPORTED;
        else if (write_attr(b->iio, "fsin_pulse_limit", 0)) status = TIDE_CAM_IO;
        else b->fsin_owned = 1;
    } else if (start == 1) {
        if (read_attr(b->iio, "fsin_enable", &enabled)) status = TIDE_CAM_IO;
        else if (enabled) status = TIDE_CAM_BUSY;
        else if (write_attr(b->iio, "fsin_enable", 1)) status = TIDE_CAM_IO;
    } else {
        if (write_attr(b->iio, "fsin_enable", 0)) status = TIDE_CAM_IO;
        else b->fsin_owned = 0;
    }
    if (status) snprintf(error, TIDE_CAM_ERROR_SIZE,
        "FSIN %s failed: requires idle SC7U22 INT1/FSIN kernel, 200 Hz IMU and sysfs write access (%s)",
        start == 0 ? "prepare" : start == 1 ? "start" : "stop", tide_cam_status_string(status));
    if (rmdir("/run/tidesense-fsin.lock") && !status) {
        snprintf(error, TIDE_CAM_ERROR_SIZE, "Cannot release FSIN control lock");
        status = TIDE_CAM_IO;
    }
    return status;
}

int tide_backend_caps(tide_cam_caps *caps)
{
    static const tide_cam_mode modes[] = {
        {640, 400, TIDE_CAM_MONO8},
        {640, 480, TIDE_CAM_MONO8},
        {1280, 800, TIDE_CAM_MONO8},
    };
    *caps = (tide_cam_caps){2, sizeof(modes) / sizeof(modes[0]), modes,
        (1u << TIDE_CAM_SYNC_NONE) | (1u << TIDE_CAM_SYNC_HARDWARE),
        TIDE_CAM_CLOCK_MONOTONIC, TIDE_CAM_TIMESTAMP_VI_ARRIVAL,
        {20, 1}, {167, 100}, {60, 1}, {20, 1}};
    return TIDE_CAM_OK;
}

static SAMPLE_SNS_TYPE_E sensor_type(const tide_cam_mode *mode, tide_cam_rate rate)
{
    if (mode->width == 640 && mode->height == 400)
        return (uint64_t)rate.num <= (uint64_t)20 * rate.den ?
            OV_OV9281_MIPI_400P_20FPS_10BIT : OV_OV9281_MIPI_400P_60FPS_10BIT;
    if (mode->width == 640 && mode->height == 480)
        return OV_OV9281_MIPI_480P_60FPS_10BIT;
    return OV_OV9281_MIPI_800P_60FPS_10BIT; /* Validated 1280x800 mode. */
}

int tide_backend_close(tide_cam_backend *b, char *error)
{
    if (!b) return TIDE_CAM_OK;
    int result = TIDE_CAM_OK;
    if (b->fsin_owned) result = fsin(b, -1, error);
    for (int p = 0; p < 2; ++p) if (b->held[p]) {
        int ret = CVI_VI_ReleaseChnFrame(0, p, &b->frames[p]);
        if (ret && !result) result = sdk_error(error, "CVI_VI_ReleaseChnFrame", ret);
    }
    if (b->vi_ready) {
        int ret = SAMPLE_COMM_VI_DestroyIsp(&b->vi);
        if (ret && !result) result = sdk_error(error, "SAMPLE_COMM_VI_DestroyIsp", ret);
        ret = SAMPLE_COMM_VI_DestroyVi(&b->vi);
        if (ret && !result) result = sdk_error(error, "SAMPLE_COMM_VI_DestroyVi", ret);
    }
    if (b->sys_ready) SAMPLE_COMM_SYS_Exit();
    if (b->lock_fd >= 0) close(b->lock_fd);
    free(b);
    return result;
}

int tide_backend_open(const tide_cam_config *config, tide_cam_backend **out, char *error)
{
    *out = NULL;
    tide_cam_caps caps;
    tide_backend_caps(&caps);
    tide_cam_rate rate = config->sync == TIDE_CAM_SYNC_HARDWARE ?
        caps.hardware_sync_fps : config->free_run_fps;
    uint64_t period_ns = 1000000000ULL * rate.den / rate.num;
    if (config->exposure_max_us > period_ns / 1000) {
        snprintf(error, TIDE_CAM_ERROR_SIZE, "Exposure cap exceeds the nominal frame period (%llu us)",
                 (unsigned long long)(period_ns / 1000));
        return TIDE_CAM_UNSUPPORTED;
    }
    tide_cam_backend *b = calloc(1, sizeof(*b));
    if (!b) return TIDE_CAM_NO_MEMORY;
    b->config = *config;
    b->period_ns = period_ns;
    /* VI/ISP can suppress roughly 30 startup frames. Start low-rate requests
     * at 20 fps until VI is delivering, then switch before any publication. */
    b->rate_pending = config->sync == TIDE_CAM_SYNC_NONE &&
        (uint64_t)rate.num < (uint64_t)20 * rate.den;
    b->read_timeout_ms = (int)(period_ns / 1000000 + 50);
    if (b->read_timeout_ms < 100) b->read_timeout_ms = 100;
    b->warmup = 4;
    b->lock_fd = open("/run/tidesense-camera.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    int status = TIDE_CAM_BUSY;
    if (b->lock_fd < 0 || flock(b->lock_fd, LOCK_EX | LOCK_NB)) {
        snprintf(error, TIDE_CAM_ERROR_SIZE, "Camera is in use or /run is not writable");
        goto fail;
    }
    if (config->sync == TIDE_CAM_SYNC_HARDWARE) {
        if (find_fsin(b->iio)) {
            snprintf(error, TIDE_CAM_ERROR_SIZE, "SC7U22 INT1/FSIN kernel interface not found");
            status = TIDE_CAM_IO;
            goto fail;
        }
        status = fsin(b, 0, error);
        if (status) goto fail;
    }
    /* Board wiring is private. No INI parser or configuration file is used. */
    SAMPLE_INI_CFG_S ini = {0};
    ini.enSource = VI_PIPE_FRAME_SOURCE_DEV;
    ini.devNum = 2;
    static const CVI_S16 lanes[2][5] = {{0, 1, 2, -1, -1}, {3, 4, -1, -1, -1}};
    for (int p = 0; p < 2; ++p) {
        ini.enSnsType[p] = sensor_type(&config->mode, rate);
        ini.enWDRMode[p] = WDR_MODE_NONE;
        ini.s32BusId[p] = 2;
        ini.s32SnsI2cAddr[p] = p ? 0x60 : 0x10;
        ini.MipiDev[p] = p;
        ini.u8HwSync[p] = config->sync == TIDE_CAM_SYNC_HARDWARE;
        memcpy(ini.as16LaneId[p], lanes[p], sizeof(lanes[p]));
    }
#define TRY(call) do { int ret_ = (call); if (ret_) { status = sdk_error(error, #call, ret_); goto fail; } } while (0)
    TRY(CVI_VI_SetDevNum(2));
    TRY(SAMPLE_COMM_VI_IniToViCfg(&ini, &b->vi));
    for (int p = 0; p < 2; ++p) {
        b->vi.astViInfo[p].stChnInfo.enCompressMode = COMPRESS_MODE_NONE;
        b->vi.astViInfo[p].stChnInfo.enPixFormat = PIXEL_FORMAT_YUV_PLANAR_420;
    }
    VB_CONFIG_S vb = {0};
    vb.u32MaxPoolCnt = 1;
    vb.astCommPool[0].u32BlkSize = COMMON_GetPicBufferSize(config->mode.width,
        config->mode.height, PIXEL_FORMAT_YUV_PLANAR_420, DATA_BITWIDTH_8,
        COMPRESS_MODE_NONE, DEFAULT_ALIGN);
    vb.astCommPool[0].u32BlkCnt = 12;
    b->sys_ready = 1;
    TRY(SAMPLE_COMM_SYS_Init(&vb));
    /* SAMPLE_PLAT_VI_INIT cleans up its partial initialization on failure.
     * Unlike SAMPLE_PLAT_SYS_INIT, these calls do not install signal handlers. */
    TRY(SAMPLE_PLAT_VI_INIT(&b->vi));
    b->vi_ready = 1;
    for (int p = 0; p < 2; ++p) {
        /* The SDK names the 480/800 modes after their 60 fps maximum. Set
         * the requested sensor/AE cadence before arming FSIN or publishing
         * free-running frames; this is not post-capture frame dropping. */
        ISP_PUB_ATTR_S pub;
        TRY(CVI_ISP_GetPubAttr(p, &pub));
        pub.f32FrameRate = b->rate_pending ? 20.0f : (CVI_FLOAT)rate.num / rate.den;
        TRY(CVI_ISP_SetPubAttr(p, &pub));
        TRY(ov9281_isp_profile(p, 0, 0));
        ISP_EXPOSURE_ATTR_S ae;
        TRY(CVI_ISP_GetExposureAttr(p, &ae));
        ae.stAuto.stExpTimeRange.u32Max = config->exposure_max_us;
        if (ae.stAuto.stExpTimeRange.u32Min > config->exposure_max_us)
            ae.stAuto.stExpTimeRange.u32Min = config->exposure_max_us;
        TRY(CVI_ISP_SetExposureAttr(p, &ae));
    }
    if (b->fsin_owned) {
        status = fsin(b, 1, error);
        if (status) goto fail;
    }
    b->progress_ns = monotonic_ns();
    *out = b;
    return TIDE_CAM_OK;
fail: {
    char cleanup_error[TIDE_CAM_ERROR_SIZE] = "";
    int cleanup = tide_backend_close(b, cleanup_error);
    if (cleanup) fprintf(stderr, "tide_cam: cleanup after open failure: %s\n", cleanup_error);
    return status;
}
#undef TRY
}

int tide_backend_read(tide_cam_backend *b, tide_cam_frame_set *out, uint8_t *pixels, char *error)
{
    for (int p = 0; p < 2; ++p) if (!b->held[p]) {
        int ret = CVI_VI_GetChnFrame(0, p, &b->frames[p], b->read_timeout_ms);
        if (ret) {
            /* This SDK collapses a timed-out get-frame ioctl to
             * FAILED_NOT_ENABLED as well. Allow bounded startup/stall retries;
             * do not mistake the first 100 ms timeout for disabled hardware. */
            if ((unsigned)ret != (unsigned)CVI_ERR_VI_BUF_EMPTY &&
                (unsigned)ret != (unsigned)CVI_ERR_VI_FAILED_NOT_ENABLED)
                return sdk_error(error, "CVI_VI_GetChnFrame", ret);
            if (monotonic_ns() - b->progress_ns > 3000000000ULL) {
                snprintf(error, TIDE_CAM_ERROR_SIZE, "VI has not delivered a valid pair for 3 seconds");
                return TIDE_CAM_IO;
            }
            return TIDE_CAM_TIMEOUT;
        }
        b->held[p] = 1;
        VIDEO_FRAME_S *f = &b->frames[p].stVFrame;
        if (f->u32Width != b->config.mode.width || f->u32Height != b->config.mode.height ||
            f->u32Stride[0] < f->u32Width || f->u64PTS <= b->previous[p] ||
            f->u64PTS > UINT64_MAX / 1000) {
            snprintf(error, TIDE_CAM_ERROR_SIZE, "Invalid VI dimensions, stride or timestamp");
            return TIDE_CAM_DISCONTINUITY;
        }
        b->previous[p] = f->u64PTS;
    }
    uint64_t left = b->frames[0].stVFrame.u64PTS, right = b->frames[1].stVFrame.u64PTS;
    uint64_t skew = left > right ? left - right : right - left;
    uint64_t limit = b->config.sync == TIDE_CAM_SYNC_HARDWARE ?
        b->period_ns * 2 / 5000 : b->period_ns / 2000; /* PTS is microseconds. */
    if (skew > limit) {
        int p = left < right ? 0 : 1;
        int ret = CVI_VI_ReleaseChnFrame(0, p, &b->frames[p]);
        if (ret) return sdk_error(error, "CVI_VI_ReleaseChnFrame", ret);
        b->held[p] = 0;
        if ((b->sequence && b->config.queue == TIDE_CAM_FIFO) || ++b->unmatched > 100 ||
            monotonic_ns() - b->progress_ns > 3000000000ULL) {
            snprintf(error, TIDE_CAM_ERROR_SIZE, "VI stereo pairing lost");
            return TIDE_CAM_DISCONTINUITY;
        }
        return TIDE_CAM_TIMEOUT;
    }
    b->unmatched = 0;
    if (b->rate_pending) {
        for (int p = 0; p < 2; ++p) {
            ISP_PUB_ATTR_S pub;
            int ret = CVI_ISP_GetPubAttr(p, &pub);
            if (ret) return sdk_error(error, "CVI_ISP_GetPubAttr", ret);
            pub.f32FrameRate = (CVI_FLOAT)b->config.free_run_fps.num / b->config.free_run_fps.den;
            ret = CVI_ISP_SetPubAttr(p, &pub);
            if (ret) return sdk_error(error, "CVI_ISP_SetPubAttr", ret);
        }
        b->rate_pending = 0;
        b->warmup = 4; /* Discard queued startup frames and the rate transition. */
    }
    if (b->sequence && b->config.queue == TIDE_CAM_FIFO) for (int p = 0; p < 2; ++p) {
        VIDEO_FRAME_S *f = &b->frames[p].stVFrame;
        if ((uint32_t)(f->u32TimeRef - b->references[p]) != 1 ||
            f->u64PTS - b->delivered[p] > b->period_ns * 3 / 2000) {
            snprintf(error, TIDE_CAM_ERROR_SIZE, "VI sequence/reference/time gap in FIFO capture");
            return TIDE_CAM_DISCONTINUITY;
        }
    }
    memset(out, 0, sizeof(*out));
    out->camera_count = 2;
    out->sync = b->config.sync;
    out->clock = TIDE_CAM_CLOCK_MONOTONIC;
    out->timestamp_kind = TIDE_CAM_TIMESTAMP_VI_ARRIVAL;
    out->timestamp_ns = left * 1000;
    for (int p = 0; p < 2; ++p) {
        VIDEO_FRAME_S *f = &b->frames[p].stVFrame;
        uint32_t width = f->u32Width, height = f->u32Height;
        uint32_t len = f->u32Stride[0] * height;
        const uint8_t *y = CVI_SYS_MmapCache(f->u64PhyAddr[0], len);
        if (!y) return sdk_error(error, "CVI_SYS_MmapCache", -1);
        int ret = CVI_SYS_IonInvalidateCache(f->u64PhyAddr[0], (void *)y, len);
        if (!ret) for (uint32_t row = 0; row < height; ++row)
            memcpy(pixels + (size_t)p * width * height + (size_t)row * width,
                   y + (size_t)row * f->u32Stride[0], width);
        int unmap = CVI_SYS_Munmap((void *)y, len);
        if (ret) return sdk_error(error, "CVI_SYS_IonInvalidateCache", ret);
        if (unmap) return sdk_error(error, "CVI_SYS_Munmap", unmap);
        out->frames[p] = (tide_cam_frame){(uint32_t)p, width, height, width,
            TIDE_CAM_MONO8, pixels + (size_t)p * width * height,
            (uint64_t)width * height, f->u64PTS * 1000, f->u32TimeRef};
        b->delivered[p] = f->u64PTS;
        b->references[p] = f->u32TimeRef;
        ret = CVI_VI_ReleaseChnFrame(0, p, &b->frames[p]);
        if (ret) return sdk_error(error, "CVI_VI_ReleaseChnFrame", ret);
        b->held[p] = 0;
    }
    b->progress_ns = monotonic_ns();
    if (b->warmup) { --b->warmup; return TIDE_CAM_TIMEOUT; }
    for (int p = 0; p < 2; ++p) {
        int ret = ov9281_isp_update_black_level(p);
        if (ret) return sdk_error(error, "ov9281_isp_update_black_level", ret);
    }
    out->sequence = ++b->sequence;
    return TIDE_CAM_OK;
}
