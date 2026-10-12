/* OV9281 producer for SchurVINS. SDK libraries stay in this separate process. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "sample_comm.h"
#include "ov9281_isp.h"
#include "camera_protocol.h"
_Static_assert(sizeof(struct CameraHeader) == 56, "IPC header layout");
static volatile sig_atomic_t stopped;
static void stop_signal(int sig) { (void)sig; stopped = 1; }
#define CHECK(call) do { int err = (call); if (err) { \
 fprintf(stderr, "camera: %s failed (%#x)\n", #call, err); goto out; } } while (0)
CVI_S32 __real_CVI_VI_SetChnAttr(VI_PIPE p, VI_CHN c, VI_CHN_ATTR_S *a);
CVI_S32 __wrap_CVI_VI_SetChnAttr(VI_PIPE p, VI_CHN c, VI_CHN_ATTR_S *a) {
 VI_CHN_ATTR_S queued = *a;
 queued.u32Depth = 3; /* Must be set before channel enable on this SDK. */
 return __real_CVI_VI_SetChnAttr(p, c, &queued);
}
static int fsin(const char *action) {
 const char *script = getenv("SCHUR_FSIN_TOOL");
 char path[4096];
 if (!script) {
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path)-16);
  if (len <= 0) return -1;
  path[len] = 0;
  char *slash = strrchr(path, '/');
  if (!slash) return -1;
  strcpy(slash + 1, "fsin.sh"); script = path;
 }
 pid_t pid = fork();
 if (pid < 0) return -1;
 if (!pid) { execl(script, script, action, (char *)NULL); _exit(127); }
 int status;
 while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) return -1;
 return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
static int send_all(int fd, const void *data, size_t len) {
 const unsigned char *p = data;
 while (len && !stopped) {
  ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
  if (n < 0 && errno == EINTR) continue;
  if (n <= 0) return -1;
  p += n; len -= n;
 }
 return len ? -1 : 0;
}
int main(int argc, char **argv) {
 if (argc != 3 || strcmp(argv[1], "--fd3")) {
  fprintf(stderr, "Internal camera helper: --fd3 SENSOR.ini (connected socket on fd 3)\n"); return 2;
 }
 struct stat config_stat;
 if (stat(argv[2], &config_stat) || !S_ISREG(config_stat.st_mode) || access(argv[2], R_OK)) {
  fprintf(stderr, "camera: sensor INI must be an existing readable regular file: %s\n", argv[2]);
  return 2;
 }
 const int fd = 3;
 struct timeval timeout = {2, 0};
 if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) return 2;
 /* SDK printf logs must not enter the binary socket stream. */
 setvbuf(stdout, NULL, _IONBF, 0);
 signal(SIGPIPE, SIG_IGN);
 struct sigaction sa = {0}; sa.sa_handler = stop_signal;
 sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);
 int lock = open("/run/tidesense-camera.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
 if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) { perror("camera ownership lock"); return 1; }
 SAMPLE_INI_CFG_S ini = {0}; SAMPLE_VI_CONFIG_S vi = {0}; VB_CONFIG_S vb = {0};
 SIZE_S size[2]; VIDEO_FRAME_INFO_S frame[2] = {0};
 int held[2] = {0}, sys_ready = 0, vi_ready = 0, prepared = 0, rc = 1;
 unsigned char *pixels = NULL;
 uint64_t sent = 0, rejected = 0, prev[2] = {0}, first = 0, last = 0;
 CHECK(SAMPLE_COMM_VI_SetIniPath(argv[2])); CHECK(SAMPLE_COMM_VI_ParseIni(&ini));
 if (ini.devNum != 2 || ini.u8HwSync[0] != ini.u8HwSync[1] || ini.u8HwSync[0] > 1) {
  fprintf(stderr, "camera: two cameras with matching hw_sync=0 or 1 required\n"); goto out;
 }
 for (int p = 0; p < 2; ++p) {
  PIC_SIZE_E pic; ISP_PUB_ATTR_S pub = {0};
  CHECK(SAMPLE_COMM_VI_GetSizeBySensor(ini.enSnsType[p], &pic));
  CHECK(SAMPLE_COMM_SYS_GetPicSize(pic, &size[p]));
  CHECK(SAMPLE_COMM_SNS_GetIspAttrBySns(ini.enSnsType[p], &pub));
  if (size[p].u32Width != 640 || size[p].u32Height != 400 || pub.f32FrameRate != 20) {
   fprintf(stderr, "camera: live input currently requires 640x400 20fps on both cameras\n"); goto out;
  }
 }
 pixels = malloc(2 * 640 * 400);
 if (!pixels) goto out;
 if (ini.u8HwSync[0]) { CHECK(fsin("prepare")); prepared = 1; }
 CHECK(CVI_VI_SetDevNum(2)); CHECK(SAMPLE_COMM_VI_IniToViCfg(&ini, &vi));
 for (int p = 0; p < 2; p++) {
  vi.astViInfo[p].stChnInfo.enCompressMode = COMPRESS_MODE_NONE;
  vi.astViInfo[p].stChnInfo.enPixFormat = PIXEL_FORMAT_YUV_PLANAR_420;
 }
 vb.u32MaxPoolCnt = 1;
 vb.astCommPool[0].u32BlkSize = COMMON_GetPicBufferSize(640, 400,
  PIXEL_FORMAT_YUV_PLANAR_420, DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
 vb.astCommPool[0].u32BlkCnt = 12;
 CHECK(SAMPLE_COMM_SYS_Init(&vb)); sys_ready = 1;
 CHECK(SAMPLE_PLAT_VI_INIT(&vi)); vi_ready = 1;
 /* Override generic ISP defaults after initialization for mono OV9281. */
 for (int p = 0; p < 2; ++p) {
  CHECK(ov9281_isp_profile(p, 0, 0));
  ISP_EXPOSURE_ATTR_S ae;
  CHECK(CVI_ISP_GetExposureAttr(p, &ae));
  ISP_YNR_ATTR_S ynr;
  CHECK(CVI_ISP_GetYNRAttr(p, &ynr));
  if (ae.enOpType != OP_TYPE_AUTO ||
      ae.stAuto.stExpTimeRange.u32Max != OV9281_EXPOSURE_MAX_US) {
   fprintf(stderr, "camera: pipe %d AE mode/exposure limit did not apply\n", p);
   goto out;
  }
  if (!ynr.Enable || ynr.enOpType != OP_TYPE_AUTO ||
      ynr.stAuto.NoiseSuppressStr[0] != OV9281_YNR_STRENGTH) {
   fprintf(stderr, "camera: pipe %d YNR profile did not apply\n", p);
   goto out;
  }
  fprintf(stderr, "camera: pipe %d OV9281 mono ISP, auto exposure capped at %u us, auto gain, spatial YNR %u, sharpening/TNR off\n",
   p, OV9281_EXPOSURE_MAX_US, ynr.stAuto.NoiseSuppressStr[0]);
 }
 if (prepared) CHECK(fsin("start"));
 int warmup = 4, unmatched = 0;
 while (!stopped) {
  /* AE query is unavailable until startup frames have been processed. */
  if (sent) for (int p = 0; p < 2; ++p) CHECK(ov9281_isp_update_black_level(p));
  for (int p = 0; p < 2; ++p) if (!held[p]) {
   int ret = CVI_VI_GetChnFrame(0, p, &frame[p], prev[p] ? 1000 : 3000);
   if (ret) {
    if (stopped) rc = sent ? 0 : 1;
    else fprintf(stderr, "camera: GetChnFrame(%d) failed (%#x)\n", p, ret);
    goto out;
   }
   held[p] = 1;
   const VIDEO_FRAME_S *f = &frame[p].stVFrame;
   if (f->u32Width != 640 || f->u32Height != 400 || f->u32Stride[0] < 640 ||
       f->u64PTS <= prev[p]) { fprintf(stderr, "camera: bad dimensions or non-monotonic PTS\n"); goto out; }
   prev[p] = f->u64PTS;
  }
  uint64_t l = frame[0].stVFrame.u64PTS, r = frame[1].stVFrame.u64PTS;
  int64_t skew = (int64_t)r - (int64_t)l;
  /* Arrival PTS differ by ~2.6ms due to lane/readout paths. Never pair across
   * a 50ms trigger period. Free-run pairing is approximate, not exposure sync. */
  int64_t limit = prepared ? 20000 : 25000;
  if (skew > limit || skew < -limit) {
   int p = skew > 0 ? 0 : 1;
   CHECK(CVI_VI_ReleaseChnFrame(0, p, &frame[p])); held[p] = 0;
   ++rejected;
   if (++unmatched > 100) { fprintf(stderr, "camera: cannot pair camera streams\n"); goto out; }
   continue;
  }
  unmatched = 0;
  struct CameraHeader h = { CAMERA_MAGIC, CAMERA_VERSION, 640, 400,
   sent + 1, l * 1000, r * 1000, frame[0].stVFrame.u32TimeRef,
   frame[1].stVFrame.u32TimeRef, prepared ? CAMERA_FSIN : 0, 0 };
  if (sent && sent % 100 == 1) for (int p = 0; p < 2; ++p) {
   ISP_EXP_INFO_S exposure;
   CHECK(CVI_ISP_QueryExposureInfo(p, &exposure));
   fprintf(stderr, "camera: pipe %d actual exposure %u us, gains Q10 %u/%u/%u\n",
    p, exposure.u32ExpTime, exposure.u32AGain, exposure.u32DGain, exposure.u32ISPDGain);
  }
  for (int p = 0; p < 2; ++p) {
   VIDEO_FRAME_S *f = &frame[p].stVFrame;
   unsigned int len = f->u32Stride[0] * 400;
   const unsigned char *y = CVI_SYS_Mmap(f->u64PhyAddr[0], len);
   if (!y) goto out;
   for (unsigned int row = 0; row < 400; ++row)
    memcpy(pixels + p * 640 * 400 + row * 640, y + row * f->u32Stride[0], 640);
   CVI_SYS_Munmap((void *)y, len);
   CHECK(CVI_VI_ReleaseChnFrame(0, p, &frame[p])); held[p] = 0;
  }
  if (warmup) { --warmup; continue; }
  if (send_all(fd, &h, sizeof(h)) || send_all(fd, pixels, 2 * 640 * 400)) {
   if (!stopped && errno != EPIPE && errno != ECONNRESET) { perror("camera send"); goto out; }
   break;
  }
  if (!sent) first = l;
  last = l; ++sent;
  if (sent == 1) fprintf(stderr, "camera: first pair refs=%u/%u skew_us=%lld hw_sync=%u\n",
   h.left_ref, h.right_ref, (long long)skew, ini.u8HwSync[0]);
 }
 rc = sent ? 0 : 1;
out:
 if (prepared && fsin("stop")) { fprintf(stderr, "camera: failed to stop FSIN\n"); rc = 1; }
 for (int p = 0; p < 2; ++p) if (held[p]) CVI_VI_ReleaseChnFrame(0, p, &frame[p]);
 if (vi_ready) { SAMPLE_COMM_VI_DestroyIsp(&vi); SAMPLE_COMM_VI_DestroyVi(&vi); }
 if (sys_ready) SAMPLE_COMM_SYS_Exit();
 fprintf(stderr, "camera: sent=%llu pairing_discarded=%llu fps=%.3f\n",
  (unsigned long long)sent, (unsigned long long)rejected,
  last > first ? (sent - 1) * 1000000.0 / (last - first) : 0);
 free(pixels); close(fd); close(lock);
 return rc;
}
