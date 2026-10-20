#define _GNU_SOURCE
#include "tidesense.h"
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usb/g_uvc.h>
#include <linux/usb/video.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int uvc_ioctl(int fd, unsigned long request, void *arg, const char *name) {
 int result = ioctl(fd, request, arg);
 if (result < 0 && errno != EAGAIN && errno != EINTR &&
     !(request == VIDIOC_DQEVENT && errno == ENOENT) &&
     !((request == VIDIOC_QBUF || request == VIDIOC_DQBUF) && errno == ENODEV))
  fprintf(stderr, "uvc: %s: %s\n", name, strerror(errno));
 return result;
}
#define UVC_IOCTL(fd, request, arg) uvc_ioctl(fd, request, arg, #request)
#define TS_UVC_CONTROL_SIZE 26 /* Advertised bcdUVC=1.00. */

struct output {
 struct ts_frame *frame;
 uint8_t meta[TS_META_MAX];
 struct ts_sample samples[TS_BATCH_MAX];
 uint64_t last_frame, skipped, last_pair_drops;
 uint64_t last_sample;
 unsigned discontinuity;
};
static int source_ok(struct ts_source *src) {
 return !atomic_load(&src->error) && !atomic_load(&src->imu.error);
}
static size_t prepare(struct output *o, struct ts_source *src, uint8_t *dst, size_t capacity) {
 pthread_mutex_lock(&src->lock);
 int fresh = src->latest.seq > o->last_frame;
 if (fresh) memcpy(o->frame, &src->latest, offsetof(struct ts_frame, jpeg) + src->latest.size);
 pthread_mutex_unlock(&src->lock);
 if (!fresh) return 0;
 uint64_t cutoff = o->frame->left_ns > o->frame->right_ns ? o->frame->left_ns : o->frame->right_ns;
 uint64_t drops;
 size_t n = ts_imu_peek(&src->imu, cutoff, o->samples, TS_BATCH_MAX, &drops);
 uint64_t skipped = o->skipped;
 if (o->last_frame) skipped += o->frame->seq - o->last_frame - 1;
 unsigned flags = o->discontinuity ? TS_FLAG_DISCONTINUITY : 0;
 if (o->frame->pair_drops != o->last_pair_drops) flags |= TS_FLAG_PAIR_DROP;
 size_t size = ts_metadata(o->meta, o->frame, &src->imu, o->samples, n, drops, skipped, flags);
 size_t bytes = ts_jpeg_with_metadata(dst, capacity, o->frame, o->meta, size);
 if (!bytes) { fprintf(stderr, "uvc: JPEG plus metadata exceeds output buffer\n"); atomic_store(&src->error, 1); }
 o->last_sample = n ? o->samples[n - 1].seq : 0;
 return bytes;
}
static void accepted(struct output *o, struct ts_source *src) {
 if (o->last_frame) o->skipped += o->frame->seq - o->last_frame - 1;
 o->last_frame = o->frame->seq; o->last_pair_drops = o->frame->pair_drops;
 if (o->last_sample) ts_imu_commit(&src->imu, o->last_sample);
 o->discontinuity = 0;
}
static int ready_file(const char *path) {
 if (!path) return 0;
 FILE *f = fopen(path, "w"); if (!f) return -1;
 fprintf(f, "%ld\n", (long)getpid()); return fclose(f);
}
int ts_file_run(struct ts_source *src, const char *path, unsigned frames) {
 struct output *o = calloc(1, sizeof(*o));
 uint8_t *buf = malloc(TS_FRAME_MAX); FILE *file = NULL; int rc = -1;
 if (!o || !buf) goto done;
 o->frame = malloc(sizeof(*o->frame)); o->discontinuity = 1;
 if (!o->frame || !(file = fopen(path, "wb"))) goto done;
 unsigned sent = 0; uint64_t start = ts_now();
 while (!ts_stopped && (!frames || sent < frames) && source_ok(src)) {
  size_t n = prepare(o, src, buf, TS_FRAME_MAX);
  if (!n) { if (ts_now() - start > 5000000000ULL && !sent) break; usleep(2000); continue; }
  if (fwrite(buf, 1, n, file) != n) goto done;
  accepted(o, src); ++sent;
 }
 fprintf(stderr, "uvc: file frames=%u skipped=%llu\n", sent, (unsigned long long)o->skipped);
 rc = source_ok(src) && (!frames || sent == frames) ? 0 : -1;
done:
 if (file && fclose(file)) rc = -1;
 free(buf); if (o) { free(o->frame); free(o); } return rc;
}
static void control(struct uvc_streaming_control *c, unsigned maxpacket) {
 memset(c, 0, sizeof(*c)); c->bmHint = htole16(1);
 c->bFormatIndex = c->bFrameIndex = 1;
 c->dwFrameInterval = htole32(250000);
 c->dwMaxVideoFrameSize = htole32(TS_FRAME_MAX);
 c->dwMaxPayloadTransferSize = htole32(maxpacket);
 c->dwClockFrequency = htole32(1000000);
 c->bmFramingInfo = 3; c->bPreferedVersion = c->bMinVersion = c->bMaxVersion = 1;
}
static int setup(int fd, const struct usb_ctrlrequest *r, unsigned maxpacket, unsigned *pending,
                 struct uvc_streaming_control *probe, struct uvc_streaming_control *commit) {
 struct uvc_request_data response = { .length = -EL2HLT };
 unsigned selector = le16toh(r->wValue) >> 8;
 unsigned length = le16toh(r->wLength);
 *pending = 0;
 if ((r->bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS &&
     (r->bRequestType & USB_RECIP_MASK) == USB_RECIP_INTERFACE &&
     /* Composite descriptor order is RNDIS (0,1), then UVC (2,3). */
     (le16toh(r->wIndex) & 255) == 3 &&
     (selector == UVC_VS_PROBE_CONTROL || selector == UVC_VS_COMMIT_CONTROL)) {
  struct uvc_streaming_control current;
  switch (r->bRequest) {
  case UVC_SET_CUR:
   if ((r->bRequestType & USB_DIR_IN) || (length != 26 && length != 34)) break;
   *pending = selector; response.length = length; break;
  case UVC_GET_CUR:
   current = selector == UVC_VS_PROBE_CONTROL ? *probe : *commit;
   memcpy(response.data, &current, TS_UVC_CONTROL_SIZE); response.length = TS_UVC_CONTROL_SIZE; break;
  case UVC_GET_DEF: case UVC_GET_MIN: case UVC_GET_MAX:
   control(&current, maxpacket); memcpy(response.data, &current, TS_UVC_CONTROL_SIZE);
   response.length = TS_UVC_CONTROL_SIZE; break;
  case UVC_GET_RES:
   memset(response.data, 0, TS_UVC_CONTROL_SIZE); response.length = TS_UVC_CONTROL_SIZE; break;
  case UVC_GET_LEN:
   response.data[0] = TS_UVC_CONTROL_SIZE; response.data[1] = 0; response.length = 2; break;
  case UVC_GET_INFO:
   response.data[0] = 3; response.length = 1; break;
  }
 } else if ((le16toh(r->wIndex) & 255) == 2 && (le16toh(r->wIndex) >> 8) == 0 &&
            selector == UVC_VC_REQUEST_ERROR_CODE_CONTROL && r->bRequest == UVC_GET_CUR) {
  response.data[0] = 0; response.length = 1;
 }
 if (response.length >= 0 && response.length > (int)length) response.length = length;
 return UVC_IOCTL(fd, UVCIOC_SEND_RESPONSE, &response);
}
int ts_uvc_run(struct ts_source *src, const char *device, const char *ready, unsigned maxpacket) {
 int fd = -1, rc = -1, streaming = 0;
 struct output *o = calloc(1, sizeof(*o));
 struct { void *addr; size_t length; int queued; } buffers[3] = {0};
 enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
 if (!o) return -1;
 o->frame = malloc(sizeof(*o->frame)); o->discontinuity = 1;
 if (!o->frame) goto done;
 fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
 if (fd < 0) { perror(device); goto done; }
 struct v4l2_capability cap = {0};
 if (UVC_IOCTL(fd, VIDIOC_QUERYCAP, &cap) < 0 || strcmp((char *)cap.driver, "g_uvc")) {
  fprintf(stderr, "uvc: expected a gadget output device, got %s\n", cap.driver); goto done;
 }
 for (unsigned e = UVC_EVENT_FIRST; e <= UVC_EVENT_LAST; ++e) {
  struct v4l2_event_subscription sub = { .type = e };
  if (UVC_IOCTL(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) goto done;
 }
 struct v4l2_format format = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT };
 format.fmt.pix.width = TS_WIDTH; format.fmt.pix.height = TS_HEIGHT;
 format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG; format.fmt.pix.sizeimage = TS_FRAME_MAX;
 if (UVC_IOCTL(fd, VIDIOC_S_FMT, &format) < 0) goto done;
 struct v4l2_requestbuffers request = { .count = 3, .type = type, .memory = V4L2_MEMORY_MMAP };
 /* This SDK returns the positive buffer count from REQBUFS and may return
  * one from QBUF when resuming the queue. Only negative ioctl results fail. */
 if (UVC_IOCTL(fd, VIDIOC_REQBUFS, &request) < 0) goto done;
 if (request.count < 2 || request.count > 3) {
  fprintf(stderr, "uvc: unexpected buffer count %u\n", request.count); goto done;
 }
 for (unsigned i = 0; i < request.count; ++i) {
  struct v4l2_buffer b = { .index = i, .type = type, .memory = V4L2_MEMORY_MMAP };
  if (UVC_IOCTL(fd, VIDIOC_QUERYBUF, &b) < 0) goto done;
  buffers[i].length = b.length;
  buffers[i].addr = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
  if (buffers[i].addr == MAP_FAILED) { buffers[i].addr = NULL; goto done; }
 }
 struct uvc_streaming_control probe, commit;
 control(&probe, maxpacket); commit = probe;
 unsigned pending = 0; uint64_t transmitted = 0;
 if (ready_file(ready)) goto done;
 fprintf(stderr, "uvc: ready %s MJPEG 1280x400@40, APP15 VI/IMU timestamps, RNDIS retained\n", device);
 while (!ts_stopped && source_ok(src)) {
  /* POLLOUT is level-triggered even with no completed buffer on some gadget
   * versions. Poll events only, then nonblocking DQBUF; no busy loop. */
  struct pollfd p = {fd, POLLPRI, 0};
  int polled = poll(&p, 1, streaming ? 2 : 100);
  if (polled < 0 && errno != EINTR) goto done;
  if (p.revents & (POLLHUP | POLLNVAL)) goto done;
  if (p.revents & POLLPRI) {
   struct v4l2_event event;
   while (UVC_IOCTL(fd, VIDIOC_DQEVENT, &event) >= 0) {
    struct uvc_event *e = (void *)&event.u.data;
    switch (event.type) {
    case UVC_EVENT_SETUP:
     if (setup(fd, &e->req, maxpacket, &pending, &probe, &commit) < 0) goto done;
     break;
    case UVC_EVENT_DATA:
     if (pending && (e->data.length == 26 || e->data.length == 34)) {
      /* A single advertised mode: clamp host proposals to exactly that mode. */
      control(pending == UVC_VS_PROBE_CONTROL ? &probe : &commit, maxpacket);
     }
     pending = 0; break;
    case UVC_EVENT_STREAMON:
     if (!streaming) {
      if (UVC_IOCTL(fd, VIDIOC_STREAMON, &type) < 0) goto done;
      streaming = 1; o->discontinuity = 1;
     }
     break;
    case UVC_EVENT_STREAMOFF: case UVC_EVENT_DISCONNECT:
     if (streaming && UVC_IOCTL(fd, VIDIOC_STREAMOFF, &type) < 0 && errno != ENODEV) goto done;
     streaming = 0;
     for (unsigned i = 0; i < request.count; ++i) buffers[i].queued = 0;
     o->discontinuity = 1; break;
    }
   }
   /* v4l2_event_dequeue() uses ENOENT for an empty nonblocking event queue. */
   if (errno != EAGAIN && errno != ENOENT) goto done;
  }
  if (!streaming) continue;
  for (;;) {
   struct v4l2_buffer b = { .type = type, .memory = V4L2_MEMORY_MMAP };
   if (UVC_IOCTL(fd, VIDIOC_DQBUF, &b) < 0) {
    if (errno == ENODEV) {
     if (UVC_IOCTL(fd, VIDIOC_STREAMOFF, &type) < 0 && errno != ENODEV) goto done;
     streaming = 0; o->discontinuity = 1;
     for (unsigned j = 0; j < request.count; ++j) buffers[j].queued = 0;
    } else if (errno != EAGAIN) goto done;
    break;
   }
   if (b.index >= request.count) goto done;
   buffers[b.index].queued = 0;
  }
  if (!streaming) continue;
  for (unsigned i = 0; i < request.count; ++i) if (!buffers[i].queued) {
   size_t n = prepare(o, src, buffers[i].addr, buffers[i].length);
   if (!n) break;
   struct v4l2_buffer b = { .index = i, .type = type, .memory = V4L2_MEMORY_MMAP, .bytesused = n };
   b.timestamp.tv_sec = o->frame->left_ns / 1000000000ULL;
   b.timestamp.tv_usec = o->frame->left_ns / 1000 % 1000000;
   if (UVC_IOCTL(fd, VIDIOC_QBUF, &b) < 0) {
    /* The host can close its stream between DQEVENT and QBUF. The SDK
     * cancels the queue before delivering STREAMOFF; retain the producer
     * and the uncommitted IMU batch for the next host stream. */
    if (errno != ENODEV) goto done;
    if (UVC_IOCTL(fd, VIDIOC_STREAMOFF, &type) < 0 && errno != ENODEV) goto done;
    streaming = 0; o->discontinuity = 1;
    for (unsigned j = 0; j < request.count; ++j) buffers[j].queued = 0;
    break;
   }
   buffers[i].queued = 1; accepted(o, src); ++transmitted;
   if (transmitted % 400 == 0) fprintf(stderr, "uvc: queued=%llu skipped=%llu\n",
    (unsigned long long)transmitted, (unsigned long long)o->skipped);
  }
 }
 rc = source_ok(src) ? 0 : -1;
done:
 if (rc && !ts_stopped) perror("UVC output");
 if (fd >= 0 && streaming) UVC_IOCTL(fd, VIDIOC_STREAMOFF, &type);
 for (unsigned i = 0; i < 3; ++i) if (buffers[i].addr) munmap(buffers[i].addr, buffers[i].length);
 if (fd >= 0) close(fd);
 if (ready) unlink(ready);
 free(o->frame); free(o); return rc;
}
