#include "fpn.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TFPN_VERSION 2u
#define TFPN_HEADER_SIZE 24u
#define TFPN_MAX_BUCKETS 64u
/* Version 2 uses a new layout: flags must include all three vectors and each
 * gain bucket is gain_q10, black_q8, offset_q8[width], flat_gain_q12[width].
 * Older version-2 files that only stored offsets are intentionally rejected. */
#define TFPN_FLAG_OFFSET_Q8 1u
#define TFPN_FLAG_BLACK_Q8 2u
#define TFPN_FLAG_GAIN_Q12 4u
#define TFPN_MAX_BLACK_Q8 (255u * 256u)
#define TFPN_MIN_GAIN_Q12 2048u
#define TFPN_MAX_GAIN_Q12 8192u
/* Dark-frame column offsets are calibrated at black. The ISP tone curve
 * compresses the same sensor offset as signal rises, so scale it by
 * K / (K + signal). Half correction is reached at 48 Y LSB above black. */
#define TFPN_OFFSET_HALF_SIGNAL 48u

struct fpn_bucket {
    uint32_t gain_q10;
    uint32_t black_q8;
    int16_t *offset_q8;
    uint16_t *flat_gain_q12;
};

struct tide_fpn {
    uint16_t width, height, bucket_count;
    struct fpn_bucket *buckets;
    int16_t *current_column_q8;
    int16_t *current_column_scaled_q8;
    uint16_t *current_flat_gain_q12;
    uint32_t current_black_q8;
    uint32_t current_gain;
    int current_valid;
};

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void free_fpn(tide_fpn *fpn)
{
    if (!fpn) return;
    if (fpn->buckets) {
        for (uint16_t i = 0; i < fpn->bucket_count; ++i)
            free(fpn->buckets[i].offset_q8);
        for (uint16_t i = 0; i < fpn->bucket_count; ++i)
            free(fpn->buckets[i].flat_gain_q12);
    }
    free(fpn->buckets);
    free(fpn->current_column_q8);
    free(fpn->current_column_scaled_q8);
    free(fpn->current_flat_gain_q12);
    free(fpn);
}

void tide_fpn_free(tide_fpn *fpn)
{
    free_fpn(fpn);
}

static tide_fpn *load_failed(tide_fpn *fpn, const char *path, const char *reason)
{
    fprintf(stderr, "tide_cam: FPN LUT %s: %s\n", path, reason);
    free_fpn(fpn);
    return NULL;
}

tide_fpn *tide_fpn_load(const char *dir, int pipe, uint32_t width, uint32_t height)
{
    if (!dir || !*dir || !width || !height || width > 4096 || height > 4096)
        return NULL;
    char path[512];
    if (snprintf(path, sizeof(path), "%s/cam%d.fpn", dir, pipe) >= (int)sizeof(path))
        return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (errno != ENOENT)
            fprintf(stderr, "tide_cam: FPN LUT %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return load_failed(NULL, path, "cannot seek");
    }
    long file_size = ftell(file);
    if (file_size < (long)TFPN_HEADER_SIZE || file_size > 64 * 1024 * 1024) {
        fclose(file);
        return load_failed(NULL, path, "invalid file size");
    }
    rewind(file);
    uint8_t *data = malloc((size_t)file_size);
    if (!data) {
        fclose(file);
        return load_failed(NULL, path, "out of memory");
    }
    int read_ok = fread(data, 1, (size_t)file_size, file) == (size_t)file_size;
    fclose(file);
    if (!read_ok) {
        free(data);
        return load_failed(NULL, path, "truncated file");
    }

    tide_fpn *fpn = NULL;
    const char *reason = NULL;
    if (memcmp(data, "TFPN", 4)) { reason = "bad magic"; goto fail; }
    uint16_t version = le16(data + 4), header_size = le16(data + 6);
    uint16_t w = le16(data + 8), h = le16(data + 10);
    uint16_t count = le16(data + 12), flags = le16(data + 14);
    uint32_t data_offset = le32(data + 16);
    if (version != TFPN_VERSION || header_size != TFPN_HEADER_SIZE ||
        data_offset != TFPN_HEADER_SIZE) {
        reason = "unsupported version or header size";
        goto fail;
    }
    uint16_t required = TFPN_FLAG_OFFSET_Q8 | TFPN_FLAG_BLACK_Q8 | TFPN_FLAG_GAIN_Q12;
    if ((flags & required) != required) {
        reason = "missing black, dark-offset or flat-gain data";
        goto fail;
    }
    if (w != width || h != height) {
        reason = "image size does not match the capture mode";
        goto fail;
    }
    if (!count || count > TFPN_MAX_BUCKETS) { reason = "invalid gain bucket count"; goto fail; }
    size_t needed = TFPN_HEADER_SIZE + (size_t)count * (8 + (size_t)w * 4);
    if ((size_t)file_size != needed) { reason = "file size does not match header"; goto fail; }

    fpn = calloc(1, sizeof(*fpn));
    if (!fpn) { reason = "out of memory"; goto fail; }
    fpn->width = w;
    fpn->height = h;
    fpn->bucket_count = count;
    fpn->buckets = calloc(count, sizeof(*fpn->buckets));
    fpn->current_column_q8 = malloc((size_t)w * sizeof(*fpn->current_column_q8));
    fpn->current_column_scaled_q8 =
        malloc((size_t)w * sizeof(*fpn->current_column_scaled_q8));
    fpn->current_flat_gain_q12 = malloc((size_t)w * sizeof(*fpn->current_flat_gain_q12));
    if (!fpn->buckets || !fpn->current_column_q8 || !fpn->current_column_scaled_q8 ||
        !fpn->current_flat_gain_q12) {
        reason = "out of memory";
        goto fail;
    }

    size_t offset = TFPN_HEADER_SIZE;
    uint32_t previous_gain = 0;
    for (uint16_t i = 0; i < count; ++i) {
        uint32_t gain = le32(data + offset);
        offset += 4;
        uint32_t black = le32(data + offset);
        offset += 4;
        if (!gain || (i && gain <= previous_gain)) {
            reason = "gains are not strictly increasing";
            goto fail;
        }
        if (black > TFPN_MAX_BLACK_Q8) {
            reason = "invalid black balance offset";
            goto fail;
        }
        previous_gain = gain;
        int16_t *values = malloc((size_t)w * sizeof(*values));
        if (!values) { reason = "out of memory"; goto fail; }
        fpn->buckets[i].gain_q10 = gain;
        fpn->buckets[i].black_q8 = black;
        fpn->buckets[i].offset_q8 = values;
        for (uint32_t x = 0; x < w; ++x, offset += 2)
            values[x] = (int16_t)le16(data + offset);
        uint16_t *gains = malloc((size_t)w * sizeof(*gains));
        if (!gains) { reason = "out of memory"; goto fail; }
        fpn->buckets[i].flat_gain_q12 = gains;
        for (uint32_t x = 0; x < w; ++x, offset += 2) {
            gains[x] = le16(data + offset);
            if (gains[x] < TFPN_MIN_GAIN_Q12 || gains[x] > TFPN_MAX_GAIN_Q12) {
                reason = "invalid flat-field column gain";
                goto fail;
            }
        }
    }
    free(data);
    return fpn;

fail:
    free(data);
    return load_failed(fpn, path, reason ? reason : "invalid LUT");
}

static void select_offsets(tide_fpn *fpn, uint32_t gain)
{
    uint16_t count = fpn->bucket_count;
    if (gain <= fpn->buckets[0].gain_q10) {
        memcpy(fpn->current_column_q8, fpn->buckets[0].offset_q8,
               (size_t)fpn->width * sizeof(*fpn->current_column_q8));
        memcpy(fpn->current_flat_gain_q12, fpn->buckets[0].flat_gain_q12,
               (size_t)fpn->width * sizeof(*fpn->current_flat_gain_q12));
        fpn->current_black_q8 = fpn->buckets[0].black_q8;
    } else if (gain >= fpn->buckets[count - 1].gain_q10) {
        memcpy(fpn->current_column_q8, fpn->buckets[count - 1].offset_q8,
               (size_t)fpn->width * sizeof(*fpn->current_column_q8));
        memcpy(fpn->current_flat_gain_q12, fpn->buckets[count - 1].flat_gain_q12,
               (size_t)fpn->width * sizeof(*fpn->current_flat_gain_q12));
        fpn->current_black_q8 = fpn->buckets[count - 1].black_q8;
    } else {
        uint16_t i = 0;
        while (i + 1 < count && gain > fpn->buckets[i + 1].gain_q10) ++i;
        const struct fpn_bucket *a = &fpn->buckets[i];
        const struct fpn_bucket *b = &fpn->buckets[i + 1];
        int64_t span = (int64_t)b->gain_q10 - a->gain_q10;
        int64_t weight = (int64_t)gain - a->gain_q10;
        int64_t black_delta = (int64_t)b->black_q8 - a->black_q8;
        int64_t black_scaled = black_delta * weight;
        if (black_scaled >= 0) black_scaled += span / 2;
        else black_scaled -= span / 2;
        fpn->current_black_q8 = (uint32_t)(a->black_q8 + black_scaled / span);
        for (uint32_t x = 0; x < fpn->width; ++x) {
            int64_t delta = (int64_t)b->offset_q8[x] - a->offset_q8[x];
            int64_t scaled = delta * weight;
            if (scaled >= 0) scaled += span / 2;
            else scaled -= span / 2;
            fpn->current_column_q8[x] = (int16_t)(a->offset_q8[x] + scaled / span);
            int64_t gd = (int64_t)b->flat_gain_q12[x] - a->flat_gain_q12[x];
            int64_t gs = gd * weight;
            if (gs >= 0) gs += span / 2;
            else gs -= span / 2;
            fpn->current_flat_gain_q12[x] = (uint16_t)(a->flat_gain_q12[x] + gs / span);
        }
    }
    fpn->current_gain = gain;
    fpn->current_valid = 1;
}

int tide_fpn_apply(tide_fpn *fpn, uint32_t gain_q10, uint8_t *y,
                   uint32_t width, uint32_t height, uint32_t stride)
{
    if (!fpn || !y || !gain_q10 || width != fpn->width || height != fpn->height ||
        stride < width)
        return 0;
    if (!fpn->current_valid || gain_q10 != fpn->current_gain)
        select_offsets(fpn, gain_q10);
    uint64_t sum = 0;
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t *line = y + (size_t)row * stride;
        for (uint32_t x = 0; x < width; ++x) sum += line[x];
    }
    uint32_t mean_q8 = (uint32_t)((sum << 8) /
        ((uint64_t)width * height));
    uint32_t signal_q8 = mean_q8 > fpn->current_black_q8 ?
        mean_q8 - fpn->current_black_q8 : 0;
    uint32_t half_q8 = TFPN_OFFSET_HALF_SIGNAL * 256u;
    uint32_t alpha_q8 = (half_q8 * 256u + (half_q8 + signal_q8) / 2) /
        (half_q8 + signal_q8);
    for (uint32_t x = 0; x < width; ++x) {
        int32_t scaled = ((int32_t)fpn->current_column_q8[x] *
                          (int32_t)alpha_q8 + 128) >> 8;
        fpn->current_column_scaled_q8[x] = (int16_t)scaled;
    }
    for (uint32_t row = 0; row < height; ++row) {
        uint8_t *line = y + (size_t)row * stride;
        for (uint32_t x = 0; x < width; ++x) {
            int32_t value = ((int32_t)line[x] << 8) - (int32_t)fpn->current_black_q8 -
                            fpn->current_column_scaled_q8[x];
            value = (value * (int32_t)fpn->current_flat_gain_q12[x] + 2048) >> 12;
            value = (value + 128) >> 8;
            line[x] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
        }
    }
    return 1;
}
