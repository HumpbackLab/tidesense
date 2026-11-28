#include "backend.h"
#include "raw.h"
#include <arm_neon.h>
#include <stdlib.h>

#define RAW_BUFFERS 8u

struct tide_raw_capture {
    VB_POOL pool;
    uint32_t width, height, stride, length;
    CVI_U64 address[2][RAW_BUFFERS];
    VB_BLK block[2][RAW_BUFFERS];
    void *mapping[2][RAW_BUFFERS];
    uint32_t allocated[2], started;
};

static int raw_error(char *error, const char *operation, int status)
{
    snprintf(error, TIDE_CAM_ERROR_SIZE, "%s failed (%#x)", operation, (unsigned)status);
    return TIDE_CAM_IO;
}

int tide_raw_open(uint32_t width, uint32_t height, tide_raw_capture **out, char *error)
{
    tide_raw_capture *raw = calloc(1, sizeof(*raw));
    if (!raw) return TIDE_CAM_NO_MEMORY;
    *out = raw; /* On failure, the caller stops VI before returning buffers. */
    raw->width = width;
    raw->height = height;
    raw->stride = ALIGN(3 * width / 2, DEFAULT_ALIGN);
    raw->length = raw->stride * height;
    VB_POOL_CONFIG_S config = {.u32BlkSize = raw->length, .u32BlkCnt = 2 * RAW_BUFFERS};
    raw->pool = CVI_VB_CreatePool(&config);
    if (raw->pool == VB_INVALID_POOLID) return raw_error(error, "Allocate front-end RAW pool", -1);
    for (int pipe = 0; pipe < 2; ++pipe) {
        for (uint32_t i = 0; i < RAW_BUFFERS; ++i) {
            VB_BLK block = CVI_VB_GetBlock(raw->pool, raw->length);
            if (block == VB_INVALID_HANDLE) return raw_error(error, "Allocate front-end RAW block", -1);
            raw->block[pipe][i] = block;
            ++raw->allocated[pipe];
            CVI_U64 address = CVI_VB_Handle2PhysAddr(block);
            if (!address) return raw_error(error, "Resolve front-end RAW block address", -1);
            raw->address[pipe][i] = address;
            raw->mapping[pipe][i] = CVI_SYS_MmapCache(address, raw->length);
            if (!raw->mapping[pipe][i]) return raw_error(error, "Map front-end RAW block", -1);
        }
    }
    for (int pipe = 0; pipe < 2; ++pipe) {
        VI_SMOOTH_RAW_DUMP_INFO_S config = {0};
        config.ViPipe = pipe;
        config.u8BlkCnt = RAW_BUFFERS;
        config.phy_addr_list = raw->address[pipe];
        config.stCropRect.u32Width = width;
        config.stCropRect.u32Height = height;
        int status = CVI_VI_StartSmoothRawDump(&config);
        if (status) return raw_error(error, "Start front-end RAW capture", status);
        ++raw->started;
    }
    return TIDE_CAM_OK;
}

int tide_raw_read(VI_PIPE pipe, VIDEO_FRAME_INFO_S *frame, int timeout_ms)
{
    VIDEO_FRAME_INFO_S raw[2] = {0};
    int status = CVI_VI_GetSmoothRawDump(pipe, raw, timeout_ms);
    if (!status) *frame = raw[0];
    return status;
}

int tide_raw_release(VI_PIPE pipe, const VIDEO_FRAME_INFO_S *frame)
{
    VIDEO_FRAME_INFO_S raw[2] = {*frame};
    return CVI_VI_PutSmoothRawDump(pipe, raw);
}

int tide_raw_copy(const tide_raw_capture *raw, VI_PIPE pipe, const VIDEO_FRAME_S *frame,
                  uint8_t *pixels, char *error)
{
    if (frame->u32Length[0] != raw->length || frame->u32Stride[0] != raw->stride)
        return raw_error(error, "Validate front-end RAW format", -1);
    uint32_t i;
    for (i = 0; i < RAW_BUFFERS; ++i)
        if (raw->address[pipe][i] == frame->u64PhyAddr[0]) break;
    if (i == RAW_BUFFERS) return raw_error(error, "Validate front-end RAW address", -1);
    void *mapping = raw->mapping[pipe][i];
    int status = CVI_SYS_IonInvalidateCache(frame->u64PhyAddr[0], mapping, raw->length);
    if (status) return raw_error(error, "Invalidate front-end RAW cache", status);
    for (uint32_t row = 0; row < raw->height; ++row) {
        const uint8_t *src = (const uint8_t *)mapping + (size_t)row * raw->stride;
        uint8_t *dst = pixels + (size_t)row * raw->width;
        /* The two eight-bit samples occupy the first two bytes of each
         * three-byte DMA group. The final byte holds unused low nibbles. */
        for (uint32_t col = 0; col < raw->width; col += 32) {
            uint8x16x3_t packed = vld3q_u8(src + col * 3 / 2);
            uint8x16x2_t luma = {{packed.val[0], packed.val[1]}};
            vst2q_u8(dst + col, luma);
        }
    }
    return TIDE_CAM_OK;
}

void tide_raw_stop(tide_raw_capture *raw)
{
    if (!raw) return;
    for (uint32_t pipe = 0; pipe < raw->started; ++pipe) {
        VI_SMOOTH_RAW_DUMP_INFO_S config = {.ViPipe = pipe};
        CVI_VI_StopSmoothRawDump(&config);
    }
    raw->started = 0;
}

void tide_raw_close(tide_raw_capture *raw)
{
    if (!raw) return;
    for (int pipe = 0; pipe < 2; ++pipe) {
        for (uint32_t i = 0; i < raw->allocated[pipe]; ++i) {
            if (raw->mapping[pipe][i]) CVI_SYS_Munmap(raw->mapping[pipe][i], raw->length);
            CVI_VB_ReleaseBlock(raw->block[pipe][i]);
        }
    }
    if (raw->pool != VB_INVALID_POOLID) CVI_VB_DestroyPool(raw->pool);
    free(raw);
}
