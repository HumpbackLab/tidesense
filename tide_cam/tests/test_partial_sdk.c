/* Exercise real SG2002 channel polling/copying without camera hardware. */
#include "../src/sg2002/backend.c"
#include <assert.h>

static unsigned available = 3, calls[2], releases[2], native_held[2], sequence[2];
static int delayed_camera = -1;
static uint8_t image[2][640 * 400];

CVI_S32 CVI_VI_GetChnFrame(VI_PIPE pipe, VI_CHN channel, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout)
{
    assert(pipe == 0 && channel < 2 && timeout <= 100);
    ++calls[channel];
    if (channel == delayed_camera && timeout < 40) return CVI_ERR_VI_BUF_EMPTY;
    if (!(available & (1u << channel))) return CVI_ERR_VI_BUF_EMPTY;
    assert(!native_held[channel]);
    native_held[channel] = 1;
    memset(frame, 0, sizeof(*frame));
    frame->stVFrame.u32Width = 640;
    frame->stVFrame.u32Height = 400;
    frame->stVFrame.u32Stride[0] = 640;
    frame->stVFrame.u64PhyAddr[0] = channel + 1;
    frame->stVFrame.u32TimeRef = ++sequence[channel];
    frame->stVFrame.u64PTS = 1000000 + sequence[channel] * 50000 + channel * 2600;
    return 0;
}
CVI_S32 CVI_VI_ReleaseChnFrame(VI_PIPE pipe, VI_CHN channel, const VIDEO_FRAME_INFO_S *frame)
{
    (void)pipe; (void)frame;
    assert(native_held[channel]);
    native_held[channel] = 0;
    ++releases[channel];
    return 0;
}
void *CVI_SYS_MmapCache(CVI_U64 address, CVI_U32 size)
{
    assert(address >= 1 && address <= 2 && size == sizeof(image[0]));
    return image[address - 1];
}
CVI_S32 CVI_SYS_IonInvalidateCache(CVI_U64 address, CVI_VOID *memory, CVI_U32 size)
{ (void)address; (void)memory; (void)size; return 0; }
CVI_S32 CVI_SYS_Munmap(CVI_VOID *memory, CVI_U32 size)
{ (void)memory; (void)size; return 0; }
CVI_S32 CVI_ISP_QueryExposureInfo(VI_PIPE pipe, ISP_EXP_INFO_S *info)
{ (void)pipe; memset(info, 0, sizeof(*info)); return 0; }
CVI_S32 CVI_ISP_GetBlackLevelAttr(VI_PIPE pipe, ISP_BLACK_LEVEL_ATTR_S *attr)
{ (void)pipe; (void)attr; return 0; }
CVI_S32 CVI_ISP_SetBlackLevelAttr(VI_PIPE pipe, const ISP_BLACK_LEVEL_ATTR_S *attr)
{ (void)pipe; (void)attr; return 0; }
CVI_S32 CVI_ISP_GetPubAttr(VI_PIPE pipe, ISP_PUB_ATTR_S *attr)
{ (void)pipe; (void)attr; return 0; }
CVI_S32 CVI_ISP_SetPubAttr(VI_PIPE pipe, const ISP_PUB_ATTR_S *attr)
{ (void)pipe; (void)attr; return 0; }
int ov9281_read_register(VI_PIPE pipe, int addr)
{ (void)pipe; return addr == 0x3502 ? 0x20 : 0; }
const OV9281_MODE_S *ov9281_get_mode(VI_PIPE pipe)
{
    (void)pipe;
    static const OV9281_MODE_S mode = {.u32HtsDef = 800};
    return &mode;
}
int tide_raw_read(VI_PIPE pipe, VIDEO_FRAME_INFO_S *frame, int timeout_ms)
{
    (void)pipe; (void)frame; (void)timeout_ms;
    assert(0);
    return -1;
}
int tide_raw_release(VI_PIPE pipe, const VIDEO_FRAME_INFO_S *frame)
{
    (void)pipe; (void)frame;
    assert(0);
    return -1;
}
int tide_raw_copy(const tide_raw_capture *raw, VI_PIPE pipe, const VIDEO_FRAME_S *frame,
                  uint8_t *pixels, char *error)
{
    (void)raw; (void)pipe; (void)frame; (void)pixels; (void)error;
    assert(0);
    return -1;
}

int main(void)
{
    tide_cam_backend backend = {0};
    backend.config.mode = (tide_cam_mode){640, 400, TIDE_CAM_MONO8};
    backend.config.sync = TIDE_CAM_SYNC_HARDWARE;
    backend.config.allow_partial = 1;
    backend.period_ns = 50000000;
    backend.progress_ns = monotonic_ns();
    backend.read_timeout_ms = 100;
    memset(image[0], 31, sizeof(image[0]));
    memset(image[1], 97, sizeof(image[1]));
    uint8_t pixels[2 * 640 * 400];
    tide_cam_frame_set frames;
    char error[TIDE_CAM_ERROR_SIZE];
    for (unsigned mask = 1; mask <= 3; ++mask) {
        available = mask;
        sequence[0] = sequence[1] = mask * 10;
        assert(!tide_backend_read(&backend, &frames, pixels, error));
        assert(calls[0] >= mask && calls[1] >= mask);
        assert(frames.camera_count == (mask == 3 ? 2u : 1u));
        assert(frames.frames[0].camera_id == (mask == 2 ? 1u : 0u));
        assert(frames.timestamp_ns == frames.frames[0].timestamp_ns);
        for (unsigned p = 0; p < frames.camera_count; ++p)
            assert(frames.frames[p].data[0] == (frames.frames[p].camera_id ? 97 : 31));
        assert(!native_held[0] && !native_held[1]);
    }
    assert(releases[0] == 2 && releases[1] == 2);
    /* A healthy camera arriving 40 ms later must not flash as missing. */
    for (int p = 0; p < 2; ++p) {
        available = 3;
        delayed_camera = p;
        sequence[0] = sequence[1] = 40 + p * 10;
        assert(!tide_backend_read(&backend, &frames, pixels, error));
        assert(frames.camera_count == 2);
    }
    delayed_camera = -1;
    /* A queued old frame is drained until a real stereo pair is available. */
    sequence[0] = 60;
    sequence[1] = 62;
    backend.progress_ns = monotonic_ns();
    assert(tide_backend_read(&backend, &frames, pixels, error) == TIDE_CAM_TIMEOUT);
    assert(tide_backend_read(&backend, &frames, pixels, error) == TIDE_CAM_TIMEOUT);
    assert(!tide_backend_read(&backend, &frames, pixels, error));
    assert(frames.camera_count == 2);
    available = 0;
    backend.progress_ns = monotonic_ns() - 4000000000ULL;
    assert(tide_backend_read(&backend, &frames, pixels, error) == TIDE_CAM_IO);
    assert(strstr(error, "Neither camera"));
    /* Default stereo capture still waits for both cameras. */
    available = 2;
    backend.config.allow_partial = 0;
    backend.progress_ns = monotonic_ns();
    unsigned before = calls[1];
    assert(tide_backend_read(&backend, &frames, pixels, error) == TIDE_CAM_TIMEOUT);
    assert(calls[1] == before);
    available = 3;
    sequence[0] = sequence[1] = 0;
    tide_cam_backend synced = {0};
    synced.config.mode = backend.config.mode;
    synced.config.sync = TIDE_CAM_SYNC_SENSOR_MASTER;
    synced.config.queue = TIDE_CAM_FIFO;
    synced.period_ns = 50000000;
    synced.progress_ns = monotonic_ns();
    synced.read_timeout_ms = 100;
    int events[2];
    assert(!pipe2(events, O_NONBLOCK));
    synced.edge_fd = events[0];
    synced.edge_enabled = 1;
    for (int i = 0; i < 4; ++i) {
        struct iio_event_data edge = {.timestamp = 890000000 + i * 50000000};
        assert(write(events[1], &edge, sizeof(edge)) == sizeof(edge));
    }
    assert(!tide_backend_read(&synced, &frames, pixels, error));
    assert(frames.fsin_sequence == 4 && frames.fsin_timestamp_ns == 1040000000);
    assert(frames.exposure_ns == 20000);
    /* A missing edge makes the next frame ambiguous, even with close VI PTSes. */
    struct iio_event_data skipped = {.timestamp = 1140000000};
    assert(write(events[1], &skipped, sizeof(skipped)) == sizeof(skipped));
    assert(tide_backend_read(&synced, &frames, pixels, error) == TIDE_CAM_DISCONTINUITY);
    close(events[0]);
    close(events[1]);
    puts("SG2002 partial preview: left/right/both, recovery, release and strict stereo passed");
    return 0;
}
