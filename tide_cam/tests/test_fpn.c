#define _POSIX_C_SOURCE 200809L
#include "sg2002/fpn.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint8_t *header(uint8_t *data, uint16_t version, uint16_t width,
                       uint16_t height, uint16_t count, uint16_t flags)
{
    memcpy(data, "TFPN", 4);
    put16(data + 4, version);
    put16(data + 6, 24);
    put16(data + 8, width);
    put16(data + 10, height);
    put16(data + 12, count);
    put16(data + 14, flags);
    put32(data + 16, 24);
    put32(data + 20, 0);
    return data + 24;
}

static int write_file(const char *dir, const char *name,
                      const uint8_t *data, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    size_t written = fwrite(data, 1, size, file);
    int closed = fclose(file);
    return written == size && !closed ? 0 : -1;
}

int main(void)
{
    char dir[] = "/tmp/tide-fpn-test-XXXXXX";
    assert(mkdtemp(dir));

    uint8_t v2[24 + 2 * (8 + 4 * 2 + 4 * 2)];
    uint8_t *p = header(v2, 2, 4, 2, 2, 7);
    const uint32_t gains[2] = {1024, 2048};
    const uint32_t blacks[2] = {10 * 256, 20 * 256};
    const int16_t offsets[2][4] = {
        {256, -256, 0, 128},
        {-256, 256, 0, -128},
    };
    const uint16_t flat_gains[2][4] = {
        {4096, 4096, 8192, 2048},
        {4096, 4096, 4096, 4096},
    };
    for (unsigned b = 0; b < 2; ++b) {
        put32(p, gains[b]);
        put32(p + 4, blacks[b]);
        p += 8;
        for (unsigned x = 0; x < 4; ++x, p += 2)
            put16(p, (uint16_t)offsets[b][x]);
        for (unsigned x = 0; x < 4; ++x, p += 2)
            put16(p, flat_gains[b][x]);
    }
    assert(!write_file(dir, "cam0.fpn", v2, sizeof(v2)));

    uint8_t bad[24 + 8 + 4 * 2];
    p = header(bad, 2, 4, 2, 1, 3);
    put32(p, 1024);
    put32(p + 4, 255 * 256 + 1);
    assert(!write_file(dir, "cam2.fpn", bad, sizeof(bad)));

    uint8_t old[24 + 8 + 4 * 2];
    p = header(old, 2, 4, 2, 1, 3);
    put32(p, 1024);
    put32(p + 4, 10 * 256);
    for (unsigned x = 0; x < 4; ++x, p += 2) put16(p, 0);
    assert(!write_file(dir, "cam3.fpn", old, sizeof(old)));

    tide_fpn *lut = tide_fpn_load(dir, 0, 4, 2);
    assert(lut);
    uint8_t y[8];

    memset(y, 100, sizeof(y));
    assert(tide_fpn_apply(lut, 1024, y, 4, 2, 4) == 1);
    assert(y[0] == 90 && y[1] == 90 && y[2] == 180 && y[3] == 45);

    memset(y, 11, sizeof(y));
    assert(tide_fpn_apply(lut, 1024, y, 4, 2, 4) == 1);
    assert(y[0] == 0 && y[1] == 2 && y[2] == 2 && y[3] == 0);

    memset(y, 100, sizeof(y));
    assert(tide_fpn_apply(lut, 1536, y, 4, 2, 4) == 1);
    assert(y[0] == 85 && y[1] == 85 && y[2] == 128 && y[3] == 64);

    memset(y, 100, sizeof(y));
    assert(tide_fpn_apply(lut, 2048, y, 4, 2, 4) == 1);
    for (unsigned i = 0; i < sizeof(y); ++i) assert(y[i] == 80);

    memset(y, 5, sizeof(y));
    assert(tide_fpn_apply(lut, 1024, y, 4, 2, 4) == 1);
    for (unsigned i = 0; i < sizeof(y); ++i) assert(y[i] == 0);

    assert(tide_fpn_apply(lut, 1024, y, 5, 2, 5) == 0);
    tide_fpn_free(lut);

    assert(!tide_fpn_load(dir, 2, 4, 2));
    assert(!tide_fpn_load(dir, 3, 4, 2));
    assert(!tide_fpn_load(dir, 9, 4, 2));
    assert(!tide_fpn_load(dir, 0, 5, 2));

    char path[512];
    const unsigned files[] = {0, 2, 3};
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        snprintf(path, sizeof(path), "%s/cam%u.fpn", dir, files[i]);
        assert(!unlink(path));
    }
    assert(!rmdir(dir));
    puts("FPN black balance, column offsets and interpolation passed");
    return 0;
}
