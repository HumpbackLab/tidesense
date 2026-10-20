#include "tidesense.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

volatile sig_atomic_t ts_stopped;
static void stop(int sig) { (void)sig; ts_stopped = 1; }
static void usage(const char *program) {
 fprintf(stderr, "Usage: %s --ini SENSOR.ini [--device /dev/videoN | --capture OUTPUT.mjpg --frames N] [--quality 1..99] [--ready-file PATH] [--maxpacket 1024|2048|3072]\n", program);
}
int main(int argc, char **argv) {
 struct ts_source *src = calloc(1, sizeof(*src));
 if (!src) return 1;
 src->imu.fd = -1; src->quality = 60;
 const char *device = NULL, *capture = NULL, *ready = NULL;
 unsigned frames = 0, maxpacket = 3072;
 for (int i = 1; i < argc; ++i) {
  if (i + 1 >= argc) { usage(argv[0]); free(src); return 2; }
  const char *key = argv[i++], *v = argv[i];
  if (!strcmp(key, "--ini")) src->ini = v;
  else if (!strcmp(key, "--device")) device = v;
  else if (!strcmp(key, "--capture")) capture = v;
  else if (!strcmp(key, "--ready-file")) ready = v;
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
 if (!src->ini || stat(src->ini, &st) || !S_ISREG(st.st_mode) || (!device == !capture) ||
     (capture && !frames) || (device && frames)) { usage(argv[0]); free(src); return 2; }
 setvbuf(stdout, NULL, _IONBF, 0);
 struct sigaction sa = {0}; sa.sa_handler = stop;
 sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);
 int rc = ts_source_open(src);
 if (!rc) rc = capture ? ts_file_run(src, capture, frames) : ts_uvc_run(src, device, ready, maxpacket);
 ts_source_close(src); free(src);
 return rc ? 1 : 0;
}
