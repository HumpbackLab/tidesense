#include "tidesense.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile sig_atomic_t ts_stopped;
int main(void) {
 struct ts_frame *f = calloc(1, sizeof(*f));
 struct ts_imu *imu = calloc(1, sizeof(*imu));
 uint8_t meta[TS_META_MAX], jpeg[4096];
 struct ts_sample samples[10] = {0};
 assert(f && imu);
 pthread_mutex_init(&imu->lock, NULL);
 imu->accel_scale = .004788403; imu->gyro_scale = .001065264;
 memcpy(f->jpeg, "\xff\xd8\xff\xd9", 4); f->size = 4;
 for (unsigned frame = 1; frame <= 3; ++frame) {
  f->seq = frame; f->left_ns = 1000000000ULL + frame * 25000000ULL;
  f->right_ns = f->left_ns + 2600000; f->left_ref = frame * 2; f->right_ref = frame * 2 + 1;
  for (unsigned i = 0; i < 10; ++i) {
   samples[i].seq = (frame - 1) * 10 + i + 1;
   samples[i].ns = f->left_ns - (9 - i) * 2500000;
   samples[i].axis[0] = -32768; samples[i].axis[1] = 32767;
   samples[i].axis[2] = (int16_t)0xd9ff; /* FF D9 embedded in APP payload. */
   samples[i].axis[3] = -1234; samples[i].axis[4] = 42; samples[i].axis[5] = 0;
  }
  size_t n = ts_metadata(meta, f, imu, samples, 10, 0, 0, frame == 1 ? TS_FLAG_DISCONTINUITY : 0);
  assert(n == 96 + 10 * 32);
  assert(!ts_metadata(meta, f, imu, samples, TS_BATCH_MAX + 1, 0, 0, 0));
  assert(!ts_jpeg_with_metadata(jpeg, n + 7, f, meta, n));
  size_t size = ts_jpeg_with_metadata(jpeg, sizeof(jpeg), f, meta, n);
  assert(size == 4 + n + 4);
  assert(fwrite(jpeg, 1, size, stdout) == size);
 }
 /* A failed output must not consume IMU; successful output retires only its batch. */
 imu->count = 3;
 for (unsigned i = 0; i < 3; ++i) { imu->ring[i].seq = i + 1; imu->ring[i].ns = 100 + i; }
 uint64_t drops;
 assert(ts_imu_peek(imu, 101, samples, 10, &drops) == 2 && imu->count == 3);
 assert(ts_imu_peek(imu, 101, samples, 10, &drops) == 2 && samples[0].seq == 1);
 ts_imu_commit(imu, 2); assert(imu->count == 1 && imu->ring[imu->head].seq == 3);
 assert(ts_imu_peek(imu, 1000, samples, 1, &drops) == 1 && samples[0].seq == 3);
 /* Producer overflow between peek and commit cannot retire newly arrived samples. */
 imu->ring[imu->head].seq = 20;
 ts_imu_commit(imu, 3); assert(imu->count == 1);
 pthread_mutex_destroy(&imu->lock); free(f); free(imu); return 0;
}
