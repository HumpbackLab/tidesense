#define _GNU_SOURCE
#include "tidesense.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include <time.h>

uint64_t ts_now(void) {
 struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
 return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}
static int value(const struct ts_imu *imu, const char *name, char *out, size_t size) {
 char path[768]; snprintf(path, sizeof(path), "%s/%s", imu->sysfs, name);
 FILE *f = fopen(path, "r");
 if (!f) return -1;
 int rc = fgets(out, size, f) ? 0 : -1; fclose(f);
 if (!rc) out[strcspn(out, "\r\n")] = 0;
 return rc;
}
int ts_setting(const struct ts_imu *imu, const char *name, const char *text) {
 char path[768]; snprintf(path, sizeof(path), "%s/%s", imu->sysfs, name);
 int fd = open(path, O_WRONLY | O_CLOEXEC);
 if (fd < 0) { perror(path); return -1; }
 ssize_t n = write(fd, text, strlen(text)); int saved = errno; close(fd); errno = saved;
 if (n != (ssize_t)strlen(text)) { perror(path); return -1; }
 return 0;
}
static uint64_t getle(const uint8_t *p, unsigned n) {
 uint64_t v = 0; for (unsigned i = 0; i < n; ++i) v |= (uint64_t)p[i] << (8 * i);
 return v;
}
static void *acquire(void *arg) {
 struct ts_imu *imu = arg;
 while (!atomic_load(&imu->quit) && !ts_stopped) {
  struct pollfd p = {imu->fd, POLLIN, 0};
  int rc = poll(&p, 1, 100);
  if (rc < 0 && errno == EINTR) continue;
  if (atomic_load(&imu->quit) || ts_stopped) break;
  if (rc <= 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) goto fail;
  uint8_t buf[24 * 64]; ssize_t n = read(imu->fd, buf, sizeof(buf));
  if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
  if (n <= 0 || n % 24) goto fail;
  pthread_mutex_lock(&imu->lock);
  for (ssize_t off = 0; off < n; off += 24) {
   struct ts_sample s = {0}; s.seq = ++imu->seq; s.ns = getle(buf + off + 16, 8);
   if (!s.ns || (imu->last_ns && s.ns <= imu->last_ns)) {
    pthread_mutex_unlock(&imu->lock); goto fail;
   }
   if (imu->last_ns && s.ns - imu->last_ns > 3750000) { s.flags |= 1; ++imu->gap_events; }
   for (unsigned a = 0; a < 6; ++a) {
    s.axis[a] = (int16_t)getle(buf + off + a * 2, 2);
    if (s.axis[a] >= 32760 || s.axis[a] <= -32760) s.flags |= 2;
   }
   imu->last_ns = s.ns;
   if (imu->count == TS_RING_SIZE) {
    imu->head = (imu->head + 1) % TS_RING_SIZE; --imu->count; ++imu->drops;
   }
   imu->ring[(imu->head + imu->count++) % TS_RING_SIZE] = s;
  }
  pthread_mutex_unlock(&imu->lock);
 }
 return NULL;
fail:
 fprintf(stderr, "uvc: IMU stream stopped, invalid scan or timestamp\n");
 atomic_store(&imu->error, 1); return NULL;
}
int ts_imu_open(struct ts_imu *imu) {
 imu->fd = -1; pthread_mutex_init(&imu->lock, NULL);
 glob_t paths = {0}; char text[128], device[128];
 if (glob("/sys/bus/iio/devices/iio:device*", 0, NULL, &paths)) return -1;
 int found = 0;
 for (size_t i = 0; i < paths.gl_pathc; ++i) {
  char resolved[4096];
  if (!realpath(paths.gl_pathv[i], resolved) || !strstr(resolved, "/41a0000.spi2/")) continue;
  snprintf(imu->sysfs, sizeof(imu->sysfs), "%s", paths.gl_pathv[i]);
  if (!value(imu, "name", text, sizeof(text)) && !strcmp(text, "sc7u22")) { found = 1; break; }
 }
 globfree(&paths);
 if (!found) { fprintf(stderr, "uvc: SC7U22 IIO on SPI2 missing\n"); return -1; }
 snprintf(device, sizeof(device), "/dev/%s", strrchr(imu->sysfs, '/') + 1);
 imu->fd = open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
 if (imu->fd < 0 || flock(imu->fd, LOCK_EX | LOCK_NB)) { perror("IMU ownership"); return -1; }
 if (value(imu, "buffer/enable", text, sizeof(text)) || strcmp(text, "0") ||
     value(imu, "fsin_enable", text, sizeof(text)) || strcmp(text, "0")) {
  fprintf(stderr, "uvc: stop existing IMU/FSIN consumer before starting\n"); return -1;
 }
 if (value(imu, "in_accel_sampling_frequency", text, sizeof(text))) return -1;
 imu->old_rate = strtoul(text, NULL, 10);
 if (imu->old_rate != 200 && imu->old_rate != 400) return -1;
 if (ts_setting(imu, "in_accel_sampling_frequency", "400")) {
  fprintf(stderr, "uvc: install the TideSense 200/400 Hz kernel first\n"); return -1;
 }
 imu->rate_changed = 1;
 if (value(imu, "in_anglvel_sampling_frequency", text, sizeof(text)) || strcmp(text, "400")) return -1;
 if (ts_setting(imu, "current_timestamp_clock", "monotonic")) return -1;
 const char *channels[] = {"in_accel_x", "in_accel_y", "in_accel_z",
  "in_anglvel_x", "in_anglvel_y", "in_anglvel_z", "in_timestamp"};
 for (unsigned i = 0; i < 7; ++i) {
  char attr[128], index[16]; snprintf(index, sizeof(index), "%u", i);
  snprintf(attr, sizeof(attr), "scan_elements/%s_index", channels[i]);
  if (value(imu, attr, text, sizeof(text)) || strcmp(text, index)) return -1;
  snprintf(attr, sizeof(attr), "scan_elements/%s_type", channels[i]);
  if (value(imu, attr, text, sizeof(text)) || strcmp(text, i == 6 ? "le:s64/64>>0" : "le:s16/16>>0")) return -1;
  snprintf(attr, sizeof(attr), "scan_elements/%s_en", channels[i]);
  if (ts_setting(imu, attr, "1")) return -1;
 }
 if (value(imu, "in_accel_scale", text, sizeof(text))) return -1;
 imu->accel_scale = strtod(text, NULL);
 if (value(imu, "in_anglvel_scale", text, sizeof(text))) return -1;
 imu->gyro_scale = strtod(text, NULL);
 if (imu->accel_scale <= 0 || imu->gyro_scale <= 0) return -1;
 if (ts_setting(imu, "fsin_pulse_limit", "0") || ts_setting(imu, "buffer/length", "1024") ||
     ts_setting(imu, "buffer/watermark", "1") || ts_setting(imu, "buffer/enable", "1")) return -1;
 imu->enabled = 1;
 if (pthread_create(&imu->thread, NULL, acquire, imu)) return -1;
 imu->thread_started = 1;
 return 0;
}
void ts_imu_close(struct ts_imu *imu) {
 if (imu->fsin_started) ts_setting(imu, "fsin_enable", "0");
 atomic_store(&imu->quit, 1);
 if (imu->thread_started) pthread_join(imu->thread, NULL);
 if (imu->enabled) ts_setting(imu, "buffer/enable", "0");
 if (imu->rate_changed) {
  char rate[16]; snprintf(rate, sizeof(rate), "%u", imu->old_rate);
  ts_setting(imu, "in_accel_sampling_frequency", rate);
 }
 if (imu->fd >= 0) close(imu->fd);
 fprintf(stderr, "uvc: IMU samples=%llu gap_events=%llu ring_drops=%llu\n",
  (unsigned long long)imu->seq, (unsigned long long)imu->gap_events, (unsigned long long)imu->drops);
 pthread_mutex_destroy(&imu->lock);
}
size_t ts_imu_peek(struct ts_imu *imu, uint64_t cutoff, struct ts_sample *out,
                  size_t capacity, uint64_t *drops) {
 size_t n = 0; pthread_mutex_lock(&imu->lock);
 while (n < imu->count && n < capacity) {
  struct ts_sample s = imu->ring[(imu->head + n) % TS_RING_SIZE];
  if (s.ns > cutoff) break;
  out[n++] = s;
 }
 *drops = imu->drops; pthread_mutex_unlock(&imu->lock); return n;
}
void ts_imu_commit(struct ts_imu *imu, uint64_t seq) {
 pthread_mutex_lock(&imu->lock);
 while (imu->count && imu->ring[imu->head].seq <= seq) {
  imu->head = (imu->head + 1) % TS_RING_SIZE; --imu->count;
 }
 pthread_mutex_unlock(&imu->lock);
}
