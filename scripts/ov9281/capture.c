/* Board smoke test: real dual-camera VI frames, dimensions and timestamps. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "ov9281_cmos_ex.h"
#include "sample_comm.h"
#include "cvi_ae.h"

#define CHECK(call) do { int r = (call); if (r) { \
 fprintf(stderr, "%s failed: %#x\n", #call, r); goto out; } } while (0)

/* The SDK sample enables channels with depth 0 (delivery only while waiting).
 * Set a bounded queue before EnableChn so sequential stereo reads tolerate
 * scheduling delays. This SDK's SetChnAttr cannot safely be called again after
 * initialization: it appends channels rather than updating existing ones. */
CVI_S32 __real_CVI_VI_SetChnAttr(VI_PIPE pipe, VI_CHN chn, VI_CHN_ATTR_S *attr);
CVI_S32 __wrap_CVI_VI_SetChnAttr(VI_PIPE pipe, VI_CHN chn, VI_CHN_ATTR_S *attr)
{
 VI_CHN_ATTR_S queued = *attr;
 queued.u32Depth = 3;
 return __real_CVI_VI_SetChnAttr(pipe, chn, &queued);
}

/* Use hardware PWM8, never a userspace sleep loop, for the test pulses. */
static int fsin_control(const char *action, int hz, int count)
{
 pid_t pid = fork();
 if (pid < 0) return -1;
 if (pid == 0) {
  char rate[24], pulses[24];
  snprintf(rate, sizeof(rate), "%d", hz);
  snprintf(pulses, sizeof(pulses), "%d", count);
  const char *tool = "/mnt/data/ov9281-update/fsin.sh";
  if (!strcmp(action, "train")) execl(tool, tool, action, rate, pulses, (char *)NULL);
  else execl(tool, tool, action, (char *)NULL);
  perror("fsin.sh");
  _exit(127);
 }
 int status;
 if (waitpid(pid, &status, 0) < 0) return -1;
 return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int expect_no_frames(const char *phase)
{
 for (int p = 0; p < 2; p++) {
  VIDEO_FRAME_INFO_S f = {0};
  if (CVI_VI_GetChnFrame(0, p, &f, 300) == CVI_SUCCESS) {
   CVI_VI_ReleaseChnFrame(0, p, &f);
   fprintf(stderr, "%s: pipe %d emitted a frame without a trigger\n", phase, p);
   return -1;
  }
 }
 printf("PASS %s: neither pipe emitted a frame (300ms timeout each, expected)\n", phase);
 return 0;
}

int main(int argc, char **argv)
{
 SAMPLE_INI_CFG_S ini = {0};
 SAMPLE_VI_CONFIG_S vi = {0};
 VB_CONFIG_S vb = {0};
 SIZE_S size[2];
 float nominal_fps[2];
 VIDEO_FRAME_INFO_S saved[2] = {0};
 int held[2] = {0};
 PIC_SIZE_E pic;
 CVI_U64 first[2] = {0}, last[2] = {0};
 long long min_delta = 0, max_delta = 0;
 int rc = 1, sys_ready = 0, vi_ready = 0;
 int frames = 600;
 int fsin_test = argc == 3 && !strcmp(argv[2], "--fsin-test");
 int fsin_max_exposure = fsin_test && getenv("OV9281_FSIN_MAX_EXPOSURE") != NULL;
 int fsin_prepared = 0;
 int warmup = fsin_test ? 4 : 0;
 setvbuf(stdout, NULL, _IONBF, 0);
 if (argc > 3) {
  fprintf(stderr, "Usage: %s [sensor_cfg.ini [--free-run|--fsin-test]]\n", argv[0]);
  return 2;
 }
 if (argc == 3 && strcmp(argv[2], "--free-run") && !fsin_test) return 2;
 CHECK(SAMPLE_COMM_VI_SetIniPath(argc > 1 ? argv[1] : "/mnt/data/sensor_cfg.ini"));
 CHECK(SAMPLE_COMM_VI_ParseIni(&ini));
 if (ini.devNum != 2) { fprintf(stderr, "Expected two cameras\n"); return 2; }
 for (int p = 0; p < 2; p++) {
  if (argc == 3 && !fsin_test) ini.u8HwSync[p] = 0;
  if (fsin_test && ini.u8HwSync[p] != 1) { fprintf(stderr, "FSIN test requires hw_sync=1\n"); goto out; }
  CHECK(SAMPLE_COMM_VI_GetSizeBySensor(ini.enSnsType[p], &pic));
  CHECK(SAMPLE_COMM_SYS_GetPicSize(pic, &size[p]));
  ISP_PUB_ATTR_S pub = {0};
  CHECK(SAMPLE_COMM_SNS_GetIspAttrBySns(ini.enSnsType[p], &pub));
  nominal_fps[p] = pub.f32FrameRate;
  printf("pipe %d: %ux%u nominal_fps=%.0f hw_sync=%u\n", p, size[p].u32Width,
   size[p].u32Height, nominal_fps[p], ini.u8HwSync[p]);
 }
 if (size[0].u32Width != size[1].u32Width || size[0].u32Height != size[1].u32Height) {
  fprintf(stderr, "This stereo test requires equal image sizes\n");
  goto out;
 }
 if (fsin_test) {
  CHECK(fsin_control("prepare", 0, 0));
  fsin_prepared = 1;
 }
 CHECK(CVI_VI_SetDevNum(2));
 CHECK(SAMPLE_COMM_VI_IniToViCfg(&ini, &vi));
 for (int p = 0; p < 2; p++) {
  vi.astViInfo[p].stChnInfo.enCompressMode = COMPRESS_MODE_NONE;
  vi.astViInfo[p].stChnInfo.enPixFormat = PIXEL_FORMAT_YUV_PLANAR_420;
 }
 vb.u32MaxPoolCnt = 1;
 vb.astCommPool[0].u32BlkSize = COMMON_GetPicBufferSize(size[0].u32Width, size[0].u32Height,
  PIXEL_FORMAT_YUV_PLANAR_420, DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
 vb.astCommPool[0].u32BlkCnt = 12;
 CHECK(SAMPLE_COMM_SYS_Init(&vb));
 sys_ready = 1;
 CHECK(SAMPLE_PLAT_VI_INIT(&vi));
 vi_ready = 1;
 if (fsin_test) {
  if (fsin_max_exposure) {
   for (int p = 0; p < 2; p++) {
    ISP_EXPOSURE_ATTR_S exp;
    CHECK(CVI_ISP_GetExposureAttr(p, &exp));
    exp.enOpType = OP_TYPE_MANUAL;
    exp.stManual.enExpTimeOpType = OP_TYPE_MANUAL;
    exp.stManual.u32ExpTime = 100000; /* Must clamp to the FSIN limit. */
    CHECK(CVI_ISP_SetExposureAttr(p, &exp));
   }
  }
  CHECK(expect_no_frames("before FSIN"));
  /* Let VI/ISP finish its startup frame suppression, then count a fresh
   * finite train. Startup sensor interrupts are not all delivered by VI. */
  CHECK(fsin_control("start", 0, 0));
  for (int n = 0; n < 4; n++) {
   for (int p = 0; p < 2; p++) {
    VIDEO_FRAME_INFO_S f = {0};
    CHECK(CVI_VI_GetChnFrame(0, p, &f, 3000));
    CHECK(CVI_VI_ReleaseChnFrame(0, p, &f));
   }
  }
  CHECK(fsin_control("stop", 0, 0));
  for (int p = 0; p < 2; p++) {
   int n;
   for (n = 0; n < 120; n++) {
    VIDEO_FRAME_INFO_S f = {0};
    if (CVI_VI_GetChnFrame(0, p, &f, 300) != CVI_SUCCESS) break;
    CHECK(CVI_VI_ReleaseChnFrame(0, p, &f));
   }
   if (n == 120) { fprintf(stderr, "Frames continued after stopping FSIN\n"); goto out; }
  }
  CHECK(expect_no_frames("after warmup stop"));
  CHECK(fsin_control("train", 20, frames + warmup));
 }
 for (int n = -warmup; n < frames; n++) {
  for (int p = 0; p < 2; p++) {
   VIDEO_FRAME_INFO_S f = {0};
   int get_rc = CVI_VI_GetChnFrame(0, p, &f, 3000);
   if (get_rc) { fprintf(stderr, "pipe %d frame %d timeout: %#x\n", p, n, get_rc); fsin_control("status", 0, 0); system("cat /proc/cvitek/vi"); goto out; }
   int valid = f.stVFrame.u32Width == size[p].u32Width &&
    f.stVFrame.u32Height == size[p].u32Height &&
    (n <= 0 || f.stVFrame.u64PTS > last[p]);
   if (n == 0) first[p] = f.stVFrame.u64PTS;
   last[p] = f.stVFrame.u64PTS;
   if (n % 100 == 0 || n == frames - 1)
    printf("pipe %d frame %d: %ux%u pts=%llu\n", p, n,
     f.stVFrame.u32Width, f.stVFrame.u32Height, (unsigned long long)last[p]);
   if (n == frames - 1) { saved[p] = f; held[p] = 1; }
   else CHECK(CVI_VI_ReleaseChnFrame(0, p, &f));
   if (!valid) { fprintf(stderr, "Invalid dimensions or timestamp\n"); goto out; }
  }
  if (n < 0) continue;
  long long delta = (long long)last[1] - (long long)last[0];
  if (n == 0 || delta < min_delta) min_delta = delta;
  if (n == 0 || delta > max_delta) max_delta = delta;
 }
 if (fsin_test) {
  CHECK(fsin_control("status", 0, 0));
  CHECK(expect_no_frames("after finite FSIN train"));
  CHECK(fsin_control("stop", 0, 0));
 }
 printf("VI arrival PTS delta right-left: min=%lld max=%lld us; not an exposure sync measurement\n",
  min_delta, max_delta);
 for (int p = 0; p < 2; p++) {
  int w = (ov9281_read_register(p, 0x3808) << 8) | ov9281_read_register(p, 0x3809);
  int h = (ov9281_read_register(p, 0x380a) << 8) | ov9281_read_register(p, 0x380b);
  printf("pipe %d sensor output registers: %dx%d, lane_ctrl=%#x sync=%#x\n", p, w, h,
   ov9281_read_register(p, 0x3662), ov9281_read_register(p, 0x3823));
  int vts = (ov9281_read_register(p, 0x380e) << 8) | ov9281_read_register(p, 0x380f);
  printf("pipe %d timing: HTS=%d VTS=%d exposure_lines=%d\n", p,
   (ov9281_read_register(p, 0x380c) << 8) | ov9281_read_register(p, 0x380d),
   vts,
   ((ov9281_read_register(p, 0x3500) << 16) | (ov9281_read_register(p, 0x3501) << 8) |
    ov9281_read_register(p, 0x3502)) >> 4);
  if (fsin_max_exposure && (((ov9281_read_register(p, 0x3500) << 16) |
   (ov9281_read_register(p, 0x3501) << 8) | ov9281_read_register(p, 0x3502)) >> 4) != 546) {
   fprintf(stderr, "FSIN exposure did not clamp to 546 lines\n"); goto out;
  }
  if (w != (int)size[p].u32Width || h != (int)size[p].u32Height) goto out;
  if (vts != (ini.u8HwSync[p] ? 910 : (nominal_fps[p] == 20 ? 2730 : 910))) {
   fprintf(stderr, "Unexpected sensor frame period\n"); goto out;
  }
  if (fsin_test || nominal_fps[p] == 20) {
   double fps = (frames - 1) * 1000000.0 / (last[p] - first[p]);
   if (fps < 19 || fps > 21) { fprintf(stderr, "20fps mode rate out of range\n"); goto out; }
  }
 }
 for (int p = 0; p < 2; p++)
  printf("PASS pipe %d: %d frames, %.3f fps (VI PTS)\n", p, frames,
   (frames - 1) * 1000000.0 / (last[p] - first[p]));
 for (int p = 0; p < 2; p++) {
  VIDEO_FRAME_INFO_S f = saved[p];
  int valid = 1;
  CVI_U32 w = f.stVFrame.u32Width, h = f.stVFrame.u32Height;
  CVI_U32 len = f.stVFrame.u32Stride[0] * h;
  unsigned char *y = CVI_SYS_Mmap(f.stVFrame.u64PhyAddr[0], len);
  if (!y) goto out;
  char name[80];
  snprintf(name, sizeof(name), "camera%d-%ux%u.pgm", p, w, h);
  FILE *image = fopen(name, "wb");
  if (!image) { CVI_SYS_Munmap(y, len); goto out; }
  fprintf(image, "P5\n%u %u\n255\n", w, h);
  unsigned int low = 255, high = 0;
  unsigned long long sum = 0;
  for (CVI_U32 row = 0; row < h; row++) {
   unsigned char *line = y + row * f.stVFrame.u32Stride[0];
   if (fwrite(line, 1, w, image) != w) valid = 0;
   for (CVI_U32 x = 0; x < w; x++) {
    if (line[x] < low) low = line[x];
    if (line[x] > high) high = line[x];
    sum += line[x];
   }
  }
  if (fclose(image)) valid = 0;
  CVI_SYS_Munmap(y, len);
  printf("pipe %d image: min=%u max=%u mean=%.2f saved=%s\n", p, low, high,
   (double)sum / (w * h), name);
  if (low == high && !fsin_max_exposure) valid = 0;
  if (!valid) goto out;
 }
 system("cat /proc/mipi-rx; cat /proc/cvitek/vi");
 rc = 0;
out:
 if (fsin_prepared) fsin_control("stop", 0, 0);
 for (int p = 0; p < 2; p++)
  if (held[p]) CVI_VI_ReleaseChnFrame(0, p, &saved[p]);
 if (vi_ready) { SAMPLE_COMM_VI_DestroyIsp(&vi); SAMPLE_COMM_VI_DestroyVi(&vi); }
 if (sys_ready) SAMPLE_COMM_SYS_Exit();
 return rc;
}
