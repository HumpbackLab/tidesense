/* Board smoke test: real dual-camera VI frames, dimensions and timestamps. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ov9281_cmos_ex.h"
#include "sample_comm.h"

#define CHECK(call) do { int r = (call); if (r) { \
 fprintf(stderr, "%s failed: %#x\n", #call, r); goto out; } } while (0)

int main(int argc, char **argv)
{
 SAMPLE_INI_CFG_S ini = {0};
 SAMPLE_VI_CONFIG_S vi = {0};
 VB_CONFIG_S vb = {0};
 SIZE_S size[2];
 VIDEO_FRAME_INFO_S saved[2] = {0};
 int held[2] = {0};
 PIC_SIZE_E pic;
 CVI_U64 first[2] = {0}, last[2] = {0};
 long long min_delta = 0, max_delta = 0;
 int rc = 1, sys_ready = 0, vi_ready = 0;
 int frames = 600;
 setvbuf(stdout, NULL, _IONBF, 0);
 if (argc > 3) {
  fprintf(stderr, "Usage: %s [sensor_cfg.ini [--free-run]]\n", argv[0]);
  return 2;
 }
 if (argc == 3 && strcmp(argv[2], "--free-run")) return 2;
 CHECK(SAMPLE_COMM_VI_SetIniPath(argc > 1 ? argv[1] : "/mnt/data/sensor_cfg.ini"));
 CHECK(SAMPLE_COMM_VI_ParseIni(&ini));
 if (ini.devNum != 2) { fprintf(stderr, "Expected two cameras\n"); return 2; }
 for (int p = 0; p < 2; p++) {
  if (argc == 3) ini.u8HwSync[p] = 0;
  CHECK(SAMPLE_COMM_VI_GetSizeBySensor(ini.enSnsType[p], &pic));
  CHECK(SAMPLE_COMM_SYS_GetPicSize(pic, &size[p]));
  printf("pipe %d: %ux%u hw_sync=%u\n", p, size[p].u32Width, size[p].u32Height, ini.u8HwSync[p]);
 }
 if (size[0].u32Width != size[1].u32Width || size[0].u32Height != size[1].u32Height) {
  fprintf(stderr, "This stereo test requires equal image sizes\n");
  goto out;
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
 for (int p = 0; p < 2; p++) {
  VI_CHN_ATTR_S attr;
  CHECK(CVI_VI_GetChnAttr(p, p, &attr));
  attr.u32Depth = 3;
  CHECK(CVI_VI_SetChnAttr(p, p, &attr));
 }
 for (int n = 0; n < frames; n++) {
  for (int p = 0; p < 2; p++) {
   VIDEO_FRAME_INFO_S f = {0};
   CHECK(CVI_VI_GetChnFrame(0, p, &f, 3000));
   int valid = f.stVFrame.u32Width == size[p].u32Width &&
    f.stVFrame.u32Height == size[p].u32Height &&
    (n == 0 || f.stVFrame.u64PTS > last[p]);
   if (n == 0) first[p] = f.stVFrame.u64PTS;
   last[p] = f.stVFrame.u64PTS;
   if (n == 0 || n == frames - 1)
    printf("pipe %d frame %d: %ux%u pts=%llu\n", p, n,
     f.stVFrame.u32Width, f.stVFrame.u32Height, (unsigned long long)last[p]);
   if (n == frames - 1) { saved[p] = f; held[p] = 1; }
   else CHECK(CVI_VI_ReleaseChnFrame(0, p, &f));
   if (!valid) { fprintf(stderr, "Invalid dimensions or timestamp\n"); goto out; }
  }
  long long delta = (long long)last[1] - (long long)last[0];
  if (n == 0 || delta < min_delta) min_delta = delta;
  if (n == 0 || delta > max_delta) max_delta = delta;
 }
 printf("VI arrival PTS delta right-left: min=%lld max=%lld us; not an exposure sync measurement\n",
  min_delta, max_delta);
 for (int p = 0; p < 2; p++) {
  int w = (ov9281_read_register(p, 0x3808) << 8) | ov9281_read_register(p, 0x3809);
  int h = (ov9281_read_register(p, 0x380a) << 8) | ov9281_read_register(p, 0x380b);
  printf("pipe %d sensor output registers: %dx%d, lane_ctrl=%#x sync=%#x\n", p, w, h,
   ov9281_read_register(p, 0x3662), ov9281_read_register(p, 0x3823));
  printf("pipe %d timing: HTS=%d VTS=%d exposure_lines=%d\n", p,
   (ov9281_read_register(p, 0x380c) << 8) | ov9281_read_register(p, 0x380d),
   (ov9281_read_register(p, 0x380e) << 8) | ov9281_read_register(p, 0x380f),
   ((ov9281_read_register(p, 0x3500) << 16) | (ov9281_read_register(p, 0x3501) << 8) |
    ov9281_read_register(p, 0x3502)) >> 4);
  if (w != (int)size[p].u32Width || h != (int)size[p].u32Height) goto out;
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
  if (low == high) valid = 0;
  if (!valid) goto out;
 }
 system("cat /proc/mipi-rx; cat /proc/cvitek/vi");
 rc = 0;
out:
 for (int p = 0; p < 2; p++)
  if (held[p]) CVI_VI_ReleaseChnFrame(0, p, &saved[p]);
 if (vi_ready) { SAMPLE_COMM_VI_DestroyIsp(&vi); SAMPLE_COMM_VI_DestroyVi(&vi); }
 if (sys_ready) SAMPLE_COMM_SYS_Exit();
 return rc;
}
