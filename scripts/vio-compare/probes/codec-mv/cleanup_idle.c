// Only for this standalone probe, while the multimedia system is otherwise idle.
// A failed SDK sample can retain a VB init reference or an encoder channel.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "linux/cvi_vc_drv_ioctl.h"
extern int vb_ioctl_exit(int fd);
extern int vb_ioctl_get_vb_init(int fd, uint32_t *value);

int main(int argc, char **argv) {
  if (argc != 2 || strcmp(argv[1], "--probe-idle")) {
    fprintf(stderr, "usage: cleanup_idle --probe-idle (no other multimedia application running)\n");
    return 2;
  }
  int encoder = open("/dev/cvi_vc_enc0", O_RDWR);
  if (encoder >= 0) {
    fprintf(stderr, "probe stop=%d\n", ioctl(encoder, CVI_VC_VENC_STOP_RECV_FRAME));
    fprintf(stderr, "probe destroy=%d\n", ioctl(encoder, CVI_VC_VENC_DESTROY_CHN));
    close(encoder);
  }
  int base = open("/dev/cvi-base", O_RDWR);
  if (base < 0) return 1;
  for (int n = 0; n < 64; ++n) {
    uint32_t refs = 0;
    if (vb_ioctl_get_vb_init(base, &refs)) return 3;
    fprintf(stderr, "probe VB references=%u\n", refs);
    if (!refs) { close(base); return 0; }
    if (vb_ioctl_exit(base)) return 4;
  }
  close(base);
  return 5;
}
