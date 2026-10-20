#include "tidesense.h"
#include <string.h>

static void le(uint8_t *p, uint64_t v, unsigned n) {
 for (unsigned i = 0; i < n; ++i) { p[i] = (uint8_t)v; v >>= 8; }
}
static void real64(uint8_t *p, double x) {
 _Static_assert(sizeof(double) == 8, "binary64 scale");
 uint64_t v; memcpy(&v, &x, 8); le(p, v, 8);
}
size_t ts_metadata(uint8_t *out, const struct ts_frame *f, const struct ts_imu *imu,
                   const struct ts_sample *samples, size_t n, uint64_t drops,
                   uint64_t skipped, unsigned flags) {
 if (n > TS_BATCH_MAX) return 0;
 memset(out, 0, TS_META_HEADER);
 memcpy(out, "TIDESNS\0", 8);
 le(out + 8, 1, 2); le(out + 10, TS_META_HEADER, 2);
 le(out + 12, TS_SAMPLE_SIZE, 2);
 flags |= TS_FLAG_VI_MONOTONIC;
 if (drops) flags |= TS_FLAG_OVERFLOW;
 if (f->pair_drops) flags |= TS_FLAG_PAIR_DROP;
 for (size_t i = 0; i < n; ++i) if (samples[i].flags) flags |= TS_FLAG_IMU_GAP;
 le(out + 14, flags, 2);
 le(out + 16, f->seq, 8); le(out + 24, f->left_ns, 8); le(out + 32, f->right_ns, 8);
 le(out + 40, f->left_ref, 4); le(out + 44, f->right_ref, 4);
 le(out + 48, 40, 4); le(out + 52, 400, 4); le(out + 56, n, 4);
 le(out + 60, drops, 8); le(out + 68, skipped, 8);
 real64(out + 76, imu->accel_scale); real64(out + 84, imu->gyro_scale);
 for (size_t i = 0; i < n; ++i) {
  uint8_t *s = out + TS_META_HEADER + i * TS_SAMPLE_SIZE;
  le(s, samples[i].seq, 8); le(s + 8, samples[i].ns, 8);
  for (unsigned a = 0; a < 6; ++a) le(s + 16 + a * 2, (uint16_t)samples[i].axis[a], 2);
  le(s + 28, samples[i].flags, 4);
 }
 return TS_META_HEADER + n * TS_SAMPLE_SIZE;
}
size_t ts_jpeg_with_metadata(uint8_t *out, size_t capacity, const struct ts_frame *f,
                            const uint8_t *meta, size_t size) {
 if (f->size < 4 || f->jpeg[0] != 0xff || f->jpeg[1] != 0xd8 ||
     f->jpeg[f->size - 2] != 0xff || f->jpeg[f->size - 1] != 0xd9 ||
     size < TS_META_HEADER || size > 65533 || f->size > capacity ||
     size + 4 > capacity - f->size) return 0;
 out[0] = 0xff; out[1] = 0xd8;
 out[2] = 0xff; out[3] = 0xef; /* JPEG APP15, before the encoder's original markers. */
 out[4] = (size + 2) >> 8; out[5] = (size + 2) & 255;
 memcpy(out + 6, meta, size);
 memcpy(out + 6 + size, f->jpeg + 2, f->size - 2);
 return f->size + size + 4;
}
