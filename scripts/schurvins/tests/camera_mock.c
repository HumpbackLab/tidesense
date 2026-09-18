/* IPC fixture; never accesses physical cameras. */
#include "camera_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static int write_all(const void *data, size_t size) {
 const char *p = data;
 while (size) { ssize_t n = send(3, p, size, MSG_NOSIGNAL); if (n <= 0) return -1; p += n; size -= n; }
 return 0;
}
int main(void) {
 const char *mode = getenv("SCHUR_CAMERA_MOCK_MODE");
 if (!mode) return 2;
 unsigned char *pixels = malloc(640*400*2);
 if (!pixels) return 2;
 for (uint64_t seq = 1; ; ++seq) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  uint64_t t = (uint64_t)ts.tv_sec*1000000000ULL + ts.tv_nsec;
  struct CameraHeader h = {CAMERA_MAGIC, CAMERA_VERSION, 640, 400, seq, t, t+1000, seq, seq, CAMERA_FSIN, 0};
  if (!strcmp(mode, "unsynced")) h.flags = 0;
  if (!strcmp(mode, "bad-header")) h.width = 0xffffffff;
  if (!strcmp(mode, "truncated")) { write_all(&h, 12); free(pixels); return 0; }
  if (!strcmp(mode, "backwards") && seq == 2) h.left_ns = 1;
  memset(pixels, seq % 251, 640*400);
  memset(pixels+640*400, 250-(seq % 251), 640*400);
  if (write_all(&h, sizeof(h)) || write_all(pixels, 640*400*2)) break;
  usleep(!strcmp(mode, "calibration") ? 50000 : 5000);
 }
 free(pixels); return 0;
}
