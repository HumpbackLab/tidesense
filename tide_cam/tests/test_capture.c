#define _POSIX_C_SOURCE 200809L
#include "tide_cam.h"
#include "backend_mock.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void test_config_file(void)
{
    char path[] = "/tmp/tide-cam-config-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    tide_cam_config config;
    tide_cam_config_init(&config, sizeof(config));
    char error[256];
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs("# Camera setting\nexposure_max_us=2500\n", file) >= 0);
    assert(!fclose(file));
    assert(!tide_cam_config_load_file(path, &config, error, sizeof(error)));
    assert(config.exposure_max_us == 2500);
    file = fopen(path, "w");
    assert(file);
    assert(fputs("exposure_max_us=0\n", file) >= 0);
    assert(!fclose(file));
    assert(tide_cam_config_load_file(path, &config, error, sizeof(error)) == TIDE_CAM_INVALID);
    assert(config.exposure_max_us == 2500 && strstr(error, "exposure_max_us"));
    file = fopen(path, "w");
    assert(file);
    assert(fputs("exposure_max_us=1200\nunknown=1\n", file) >= 0);
    assert(!fclose(file));
    assert(tide_cam_config_load_file(path, &config, error, sizeof(error)) == TIDE_CAM_INVALID);
    assert(config.exposure_max_us == 2500);
    assert(!unlink(path));
    assert(tide_cam_config_load_file(path, &config, error, sizeof(error)) == TIDE_CAM_NOT_FOUND);
    assert(config.exposure_max_us == 2500);
}

static tide_cam *open_camera(tide_cam_queue queue, uint32_t buffers)
{
    assert(!mock_active());
    mock_reset();
    tide_cam_config config;
    tide_cam_config_init(&config, sizeof(config));
    config.queue = queue;
    config.buffer_count = buffers;
    tide_cam *camera = NULL;
    char error[256];
    assert(!tide_cam_open(&config, &camera, error, sizeof(error)));
    assert(camera && mock_active());
    return camera;
}
static tide_cam_stats wait_stats(tide_cam *camera, uint64_t captured, int terminal)
{
    tide_cam_stats stats;
    for (int i = 0; i < 2000; ++i) {
        assert(!tide_cam_get_stats(camera, &stats));
        if (stats.captured >= captured && (!terminal || stats.terminal_status == terminal)) return stats;
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
    assert(!"Timed out waiting for capture state");
    return stats;
}
static void test_validation(void)
{
    tide_cam_caps caps;
    assert(!tide_cam_query_caps(&caps));
    assert(caps.camera_count == 2 && caps.timestamp_kind == TIDE_CAM_TIMESTAMP_VI_ARRIVAL);
    tide_cam_config config;
    tide_cam_config_init(&config, sizeof(config));
    tide_cam *camera = (tide_cam *)1;
    char error[256];
    config.mode.width = 1280;
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_UNSUPPORTED);
    assert(!camera && !mock_active() && error[0]);
    config.mode.width = 640;
    config.buffer_count = 2;
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    assert(tide_cam_open(NULL, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    assert(!tide_cam_close(NULL));
    tide_cam_config_init(&config, sizeof(config));
    assert(config.fps.num == 0 && config.fps.den == 0);
    config.fps = (tide_cam_rate){20, 1};
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    assert(!mock_active() && strstr(error, "trigger"));
    tide_cam_config_init_free_run(&config, sizeof(config));
    assert(config.sync == TIDE_CAM_SYNC_NONE && config.fps.num == 20);
    const tide_cam_rate invalid[] = {{0, 1}, {20, 0}, {0, 0}};
    for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        config.fps = invalid[i];
        assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    }
    const tide_cam_rate unsupported[] = {{1, 1}, {166, 100}, {61, 1}};
    for (uint32_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        config.fps = unsupported[i];
        assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_UNSUPPORTED);
    }
    assert(!mock_active());
}
static void test_free_run_rates(void)
{
    const tide_cam_rate rates[] = {{167, 100}, {5, 1}, {10, 1}, {30, 1}, {60, 1}, {30000, 1001}};
    for (uint32_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        mock_reset();
        tide_cam_config config;
        tide_cam_config_init_free_run(&config, sizeof(config));
        config.fps = rates[i];
        config.queue = TIDE_CAM_FIFO;
        tide_cam *camera;
        char error[256];
        assert(!tide_cam_open(&config, &camera, error, sizeof(error)));
        uint64_t previous = 0;
        for (int n = 0; n < 2; ++n) {
            mock_push(1);
            const tide_cam_frame_set *frames;
            assert(!tide_cam_acquire(camera, &frames, 1000));
            assert(frames->sync == TIDE_CAM_SYNC_NONE);
            if (previous) assert(frames->timestamp_ns - previous ==
                1000000000ULL * rates[i].den / rates[i].num);
            previous = frames->timestamp_ns;
            assert(!tide_cam_release(camera, frames));
        }
        assert(!tide_cam_close(camera) && !mock_active());
    }
}
static void test_config_size(void)
{
    tide_cam_config config;
    assert(!tide_cam_config_init(&config, sizeof(config)));
    assert(config.struct_size == sizeof(config));
    assert(config.master_camera_id == TIDE_CAM_MASTER_UNSPECIFIED);
    uint32_t exposure = config.exposure_max_us;
    assert(tide_cam_config_init(&config, sizeof(config) - 1) == TIDE_CAM_INVALID);
    assert(tide_cam_config_init(&config, sizeof(config) + 4) == TIDE_CAM_UNSUPPORTED);
    assert(config.struct_size == sizeof(config) && config.exposure_max_us == exposure);

    tide_cam *camera = NULL;
    char error[256];
    config.struct_size = sizeof(config) - 1;
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    config.struct_size = sizeof(config) + 4;
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_UNSUPPORTED);
    assert(!camera && !mock_active());
    assert(tide_cam_config_load_file("/missing", &config, error, sizeof(error)) == TIDE_CAM_UNSUPPORTED);
    assert(config.exposure_max_us == exposure);
    assert(!tide_cam_config_init(&config, sizeof(config)));
    config.sync = (tide_cam_sync)99;
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_UNSUPPORTED);
    assert(!camera && !mock_active());
}
static void test_sensor_sync(void)
{
    tide_cam_config config;
    tide_cam_config_init(&config, sizeof(config));
    tide_cam *camera;
    char error[256];
    config.master_camera_id = 0;
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    config.master_camera_id = TIDE_CAM_MASTER_UNSPECIFIED;
    config.sync = TIDE_CAM_SYNC_SENSOR_MASTER;
    config.fps = (tide_cam_rate){30, 1};
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    assert(!camera);
    config.master_camera_id = 2;
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    config.master_camera_id = 0;
    config.fps.den = 0;
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    config.fps.den = 1;
    config.allow_partial = 1;
    assert(tide_cam_open(&config, &camera, error, sizeof(error)) == TIDE_CAM_INVALID);
    config.allow_partial = 0;
    for (uint32_t master = 0; master < 2; ++master) {
        mock_reset();
        config.master_camera_id = master;
        config.mode.height = master ? 480 : 400;
        config.fps.num = master ? 25 : 40;
        assert(!tide_cam_open(&config, &camera, error, sizeof(error)));
        const tide_cam_frame_set *frames;
        mock_push(1);
        assert(!tide_cam_acquire(camera, &frames, 1000));
        assert(frames->sync == TIDE_CAM_SYNC_SENSOR_MASTER);
        assert(frames->frames[0].camera_id == 0 && frames->frames[1].camera_id == 1);
        assert(frames->frames[0].height == config.mode.height);
        assert(frames->fsin_sequence == 1 && frames->exposure_ns == 500000);
        assert(frames->timestamp_ns - frames->fsin_timestamp_ns == 1000000);
        assert(!tide_cam_release(camera, frames));
        assert(!tide_cam_stop(camera) && mock_active());
        assert(!tide_cam_close(camera));
    }
}
static void test_latest_and_lease(void)
{
    tide_cam *camera = open_camera(TIDE_CAM_LATEST, 4);
    const tide_cam_frame_set *held, *latest;
    assert(tide_cam_acquire(camera, &held, 0) == TIDE_CAM_TIMEOUT && !held);
    mock_push(1);
    assert(!tide_cam_acquire(camera, &held, 1000));
    assert(held->sequence == 1 && held->frames[0].timestamp_ns == 1050000000ULL);
    assert(held->frames[1].timestamp_ns == 1052600000ULL);
    mock_push(5);
    tide_cam_stats stats = wait_stats(camera, 6, 0);
    assert(stats.queued == 1 && stats.replaced == 4 && stats.leased == 1);
    for (uint32_t p = 0; p < 2; ++p) for (uint64_t i = 0; i < held->frames[p].data_size; ++i)
        assert(held->frames[p].data[i] == 1 + p);
    assert(tide_cam_close(camera) == TIDE_CAM_BUSY);
    assert(!tide_cam_acquire(camera, &latest, 1000) && latest->sequence == 6);
    assert(!tide_cam_release(camera, latest));
    assert(tide_cam_release(camera, latest) == TIDE_CAM_INVALID);
    assert(!tide_cam_stop(camera));
    assert(!mock_active() && held->frames[0].data[0] == 1);
    assert(!tide_cam_release(camera, held));
    assert(tide_cam_acquire(camera, &latest, -1) == TIDE_CAM_STOPPED);
    assert(!tide_cam_close(camera));
}
static void test_fifo_drop_oldest(void)
{
    tide_cam *camera = open_camera(TIDE_CAM_FIFO_DROP_OLDEST, 3);
    mock_push(5);
    tide_cam_stats stats = wait_stats(camera, 5, 0);
    assert(stats.queued == 3 && stats.replaced == 2);
    for (uint64_t seq = 3; seq <= 5; ++seq) {
        const tide_cam_frame_set *frames;
        assert(!tide_cam_acquire(camera, &frames, 1000));
        assert(frames->sequence == seq);
        assert(!tide_cam_release(camera, frames));
    }
    assert(!tide_cam_close(camera));
}
static void test_resolutions(void)
{
    tide_cam_caps caps;
    assert(!tide_cam_query_caps(&caps) && caps.mode_count == 3);
    for (uint32_t m = 0; m < caps.mode_count; ++m) {
        mock_reset();
        tide_cam_config config;
        tide_cam_config_init(&config, sizeof(config));
        config.mode = caps.modes[m];
        tide_cam *camera;
        char error[256];
        assert(!tide_cam_open(&config, &camera, error, sizeof(error)));
        mock_push(1);
        const tide_cam_frame_set *frames;
        assert(!tide_cam_acquire(camera, &frames, 1000));
        uint64_t size = (uint64_t)config.mode.width * config.mode.height;
        for (uint32_t p = 0; p < 2; ++p) {
            const tide_cam_frame *f = &frames->frames[p];
            assert(f->width == config.mode.width && f->height == config.mode.height);
            assert(f->stride == config.mode.width && f->data_size == size);
            for (uint64_t i = 0; i < size; ++i) assert(f->data[i] == 1 + p);
        }
        assert(frames->frames[1].data == frames->frames[0].data + size);
        assert(!tide_cam_release(camera, frames));
        assert(!tide_cam_close(camera) && !mock_active());
    }
}
static void test_fifo_overflow(void)
{
    tide_cam *camera = open_camera(TIDE_CAM_FIFO, 3);
    mock_push(4);
    wait_stats(camera, 3, TIDE_CAM_OVERFLOW);
    assert(!tide_cam_stop(camera) && !mock_active());
    for (uint64_t i = 1; i <= 3; ++i) {
        const tide_cam_frame_set *frames;
        assert(!tide_cam_acquire(camera, &frames, 1000));
        assert(frames->sequence == i);
        assert(!tide_cam_release(camera, frames));
    }
    const tide_cam_frame_set *frames;
    assert(tide_cam_acquire(camera, &frames, 0) == TIDE_CAM_OVERFLOW);
    char error[256];
    assert(!tide_cam_get_error(camera, error, sizeof(error)) && strstr(error, "pool"));
    assert(!tide_cam_close(camera));
}
struct waiter { tide_cam *camera; int result; };
static void *wait_frame(void *arg)
{
    struct waiter *w = arg;
    const tide_cam_frame_set *frames;
    w->result = tide_cam_acquire(w->camera, &frames, -1);
    return NULL;
}
static void test_stop_and_failure(void)
{
    tide_cam *camera = open_camera(TIDE_CAM_LATEST, 4);
    struct waiter waiter = {camera, 99};
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, wait_frame, &waiter));
    assert(!tide_cam_stop(camera));
    assert(!pthread_join(thread, NULL));
    assert(waiter.result == TIDE_CAM_STOPPED && !mock_active());
    assert(!tide_cam_stop(camera));
    assert(!tide_cam_close(camera));
    camera = open_camera(TIDE_CAM_LATEST, 4);
    mock_fail(TIDE_CAM_IO);
    const tide_cam_frame_set *frames;
    assert(tide_cam_acquire(camera, &frames, 1000) == TIDE_CAM_IO);
    assert(!tide_cam_stop(camera) && !mock_active());
    char error[256];
    assert(!tide_cam_get_error(camera, error, sizeof(error)) && strstr(error, "Injected"));
    assert(!tide_cam_close(camera));
}
int main(void)
{
    test_config_file();
    test_validation();
    test_config_size();
    test_free_run_rates();
    test_sensor_sync();
    test_latest_and_lease();
    test_fifo_drop_oldest();
    test_resolutions();
    test_fifo_overflow();
    test_stop_and_failure();
    puts("tide_cam: validation, frame lifetime, latest/FIFO, overflow, stop and failure tests passed");
    return 0;
}
