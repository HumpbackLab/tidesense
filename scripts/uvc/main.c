#include "tidesense.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

volatile sig_atomic_t ts_stopped;
static void stop(int sig) { (void)sig; ts_stopped = 1; }
static void usage(const char *program) {
 fprintf(stderr, "Usage: %s --ini SENSOR.ini [--device /dev/videoN | --capture OUTPUT.mjpg --frames N] [--mode 400p40|800p60|480p180|400p210] [--master left|right] [--quality 1..99] [--ready-file PATH] [--maxpacket 1024|2048|3072]\n", program);
}
static unsigned interval(const struct ts_mode *mode) {
 return (10000000U + mode->fps / 2) / mode->fps;
}
int main(int argc, char **argv) {
 if (argc == 3 && !strcmp(argv[1], "--describe")) {
  const struct ts_mode *mode = ts_mode_find(argv[2]);
  if (!mode) { usage(argv[0]); return 2; }
  printf("%u %u %u %u %u\n", mode->width * 2, mode->height, interval(mode),
         mode->usb_interval, TS_FRAME_MAX);
  return 0;
 }
 struct ts_source *src = calloc(1, sizeof(*src));
 if (!src) return 1;
 src->imu.fd = -1;
 src->master_camera_id = 1;
 src->mode = ts_mode_find("400p40");
 const char *device = NULL, *capture = NULL, *ready = NULL;
 unsigned frames = 0, maxpacket = 3072;
 int master_selected = 0;
 for (int i = 1; i < argc; ++i) {
  if (i + 1 >= argc) { usage(argv[0]); free(src); return 2; }
  const char *key = argv[i++], *v = argv[i];
  if (!strcmp(key, "--ini")) src->ini = v;
  else if (!strcmp(key, "--mode")) src->mode = ts_mode_find(v);
  else if (!strcmp(key, "--device")) device = v;
  else if (!strcmp(key, "--capture")) capture = v;
  else if (!strcmp(key, "--ready-file")) ready = v;
  else if (!strcmp(key, "--master") && (!strcmp(v, "left") || !strcmp(v, "right"))) {
   src->master_camera_id = (unsigned)!strcmp(v, "right");
   master_selected = 1;
  }
  else {
   char *end; unsigned long n = strtoul(v, &end, 10);
   if (!*v || *end || n > 10000000) { usage(argv[0]); free(src); return 2; }
   if (!strcmp(key, "--quality") && n >= 1 && n <= 99) src->quality = n;
   else if (!strcmp(key, "--frames") && n > 0) frames = n;
   else if (!strcmp(key, "--maxpacket") && (n == 1024 || n == 2048 || n == 3072)) maxpacket = n;
   else { usage(argv[0]); free(src); return 2; }
  }
 }
 struct stat st;
 if (!src->mode || !src->ini || stat(src->ini, &st) || !S_ISREG(st.st_mode) || (!device == !capture) ||
     (capture && !frames) || (device && frames) || (master_selected && src->mode->fps == 40)) {
  usage(argv[0]); free(src); return 2;
 }
 setvbuf(stdout, NULL, _IONBF, 0);
 if (!src->quality) src->quality = src->mode->quality;
 struct sigaction sa = {0}; sa.sa_handler = stop;
 sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);
 int rc = ts_source_open(src);
 if (!rc && src->mode->fps != 40) rc = ts_source_wait_ready(src);
 if (!rc) rc = capture ? ts_file_run(src, capture, frames) : ts_uvc_run(src, device, ready, maxpacket);
 ts_source_close(src); free(src);
 return rc ? 1 : 0;
}
