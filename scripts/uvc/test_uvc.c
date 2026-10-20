/* Exercise the actual output loop with the SDK's nonstandard success returns. */
#include "tidesense.h"
#include <assert.h>
#include <errno.h>
#include <linux/usb/g_uvc.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

volatile sig_atomic_t ts_stopped;
static unsigned dequeued, queued, mapped, unmapped, failed_queue;
static struct ts_source *test_source;
int __wrap_open(const char *path, int flags, ...) {
 (void)flags; assert(!strcmp(path, "/mock/video0")); return 100;
}
int __wrap_close(int fd) { assert(fd == 100); return 0; }
void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
 (void)addr; (void)prot; (void)flags; (void)offset; assert(fd == 100);
 void *p = malloc(length); assert(p); ++mapped; return p;
}
int __wrap_munmap(void *addr, size_t length) { (void)length; free(addr); ++unmapped; return 0; }
int __wrap_poll(struct pollfd *fds, nfds_t n, int timeout) {
 (void)timeout; assert(n == 1 && fds[0].fd == 100); fds[0].revents = POLLPRI; return 1;
}
int __wrap_ioctl(int fd, unsigned long request, ...) {
 assert(fd == 100); va_list ap; va_start(ap, request); void *arg = va_arg(ap, void *); va_end(ap);
 switch (request) {
 case VIDIOC_QUERYCAP: strcpy((char *)((struct v4l2_capability *)arg)->driver, "g_uvc"); return 0;
 case VIDIOC_SUBSCRIBE_EVENT: case VIDIOC_S_FMT: return 0;
 case VIDIOC_REQBUFS: ((struct v4l2_requestbuffers *)arg)->count = 3; return 3;
 case VIDIOC_QUERYBUF: ((struct v4l2_buffer *)arg)->length = TS_FRAME_MAX; return 0;
 case VIDIOC_DQEVENT:
  if (!dequeued++) { ((struct v4l2_event *)arg)->type = UVC_EVENT_STREAMON; return 0; }
  if (failed_queue == 1) {
   assert(test_source->imu.count == 1); /* Failed QBUF must not consume IMU. */
   failed_queue = 2;
   ((struct v4l2_event *)arg)->type = UVC_EVENT_STREAMON; return 0;
  }
  errno = ENOENT; return -1;
 case VIDIOC_STREAMON: case VIDIOC_STREAMOFF: return 0;
 case VIDIOC_DQBUF: errno = EAGAIN; return -1;
 case VIDIOC_QBUF:
  assert(((struct v4l2_buffer *)arg)->bytesused > TS_META_HEADER);
  if (!failed_queue) { failed_queue = 1; errno = ENODEV; return -1; }
  ++queued; ts_stopped = 1; return 1;
 default: assert(!"unexpected ioctl"); return -1;
 }
}
int main(void) {
 struct ts_source *source = calloc(1, sizeof(*source)); assert(source);
 test_source = source;
 pthread_mutex_init(&source->lock, NULL); pthread_mutex_init(&source->imu.lock, NULL);
 source->latest.seq = 1; source->latest.left_ns = source->latest.right_ns = 1000000000;
 memcpy(source->latest.jpeg, "\xff\xd8\xff\xd9", 4); source->latest.size = 4;
 source->imu.count = 1; source->imu.ring[0].seq = 1; source->imu.ring[0].ns = 1000000000;
 source->imu.accel_scale = .004788403; source->imu.gyro_scale = .001065264;
 assert(!ts_uvc_run(source, "/mock/video0", NULL, 3072));
 assert(queued == 1 && mapped == 3 && unmapped == 3 && source->imu.count == 0);
 pthread_mutex_destroy(&source->imu.lock); pthread_mutex_destroy(&source->lock); free(source);
 puts("REQBUFS=3, QBUF=1 and empty-event ENOENT accepted; IMU committed exactly once");
 return 0;
}
