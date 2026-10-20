/* SDK-owned VI/ISP and hardware MJPEG. One owner, same lock as live VIO. */
#define _GNU_SOURCE
#include "tidesense.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include "sample_comm.h"
#include "cvi_venc.h"
#include "../schurvins/ov9281_isp.h"

static SAMPLE_VI_CONFIG_S vi;
static int camera_lock = -1, sys_ready, vi_ready, venc_ready, receiving;
static VB_BLK block = VB_INVALID_HANDLE;
static VIDEO_FRAME_INFO_S composite;
static VB_CAL_CONFIG_S layout;
static uint8_t *mapped;
#define TRY(call) do { int err = (call); if (err) { \
 fprintf(stderr, "uvc: %s failed (%#x)\n", #call, err); return -1; } } while (0)

CVI_S32 __real_CVI_VI_SetChnAttr(VI_PIPE p, VI_CHN c, VI_CHN_ATTR_S *a);
CVI_S32 __wrap_CVI_VI_SetChnAttr(VI_PIPE p, VI_CHN c, VI_CHN_ATTR_S *a) {
 VI_CHN_ATTR_S queued = *a; queued.u32Depth = 3;
 return __real_CVI_VI_SetChnAttr(p, c, &queued);
}
CVI_S32 __real_CVI_ISP_SetPubAttr(VI_PIPE p, const ISP_PUB_ATTR_S *a);
CVI_S32 __wrap_CVI_ISP_SetPubAttr(VI_PIPE p, const ISP_PUB_ATTR_S *a) {
 ISP_PUB_ATTR_S attr = *a; attr.f32FrameRate = 40;
 return __real_CVI_ISP_SetPubAttr(p, &attr);
}
static int camera_init(struct ts_source *src) {
 camera_lock = open("/run/tidesense-camera.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
 if (camera_lock < 0 || flock(camera_lock, LOCK_EX | LOCK_NB)) {
  perror("camera ownership"); return -1;
 }
 SAMPLE_INI_CFG_S ini = {0};
 TRY(SAMPLE_COMM_VI_SetIniPath(src->ini)); TRY(SAMPLE_COMM_VI_ParseIni(&ini));
 if (ini.devNum != 2 || ini.u8HwSync[0] != 1 || ini.u8HwSync[1] != 1) {
  fprintf(stderr, "uvc: two FSIN-enabled OV9281 cameras required\n"); return -1;
 }
 for (int p = 0; p < 2; ++p) {
  PIC_SIZE_E pic; SIZE_S size; ISP_PUB_ATTR_S pub;
  TRY(SAMPLE_COMM_VI_GetSizeBySensor(ini.enSnsType[p], &pic));
  TRY(SAMPLE_COMM_SYS_GetPicSize(pic, &size));
  TRY(SAMPLE_COMM_SNS_GetIspAttrBySns(ini.enSnsType[p], &pub));
  if (size.u32Width != 640 || size.u32Height != 400 || pub.f32FrameRate != 60) {
   fprintf(stderr, "uvc: INI must select OV9281 640x400 60fps timing; this application sets 40fps\n");
   return -1;
  }
 }
 TRY(CVI_VI_SetDevNum(2)); TRY(SAMPLE_COMM_VI_IniToViCfg(&ini, &vi));
 for (int p = 0; p < 2; ++p) {
  vi.astViInfo[p].stChnInfo.enCompressMode = COMPRESS_MODE_NONE;
  vi.astViInfo[p].stChnInfo.enPixFormat = PIXEL_FORMAT_YUV_PLANAR_420;
 }
 COMMON_GetPicBufferConfig(TS_WIDTH, TS_HEIGHT, PIXEL_FORMAT_YUV_PLANAR_420,
  DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN, &layout);
 VB_CONFIG_S vb = {0}; vb.u32MaxPoolCnt = 2;
 vb.astCommPool[0].u32BlkSize = COMMON_GetPicBufferSize(640, 400,
  PIXEL_FORMAT_YUV_PLANAR_420, DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
 vb.astCommPool[0].u32BlkCnt = 12;
 vb.astCommPool[1].u32BlkSize = layout.u32VBSize;
 vb.astCommPool[1].u32BlkCnt = 3;
 TRY(SAMPLE_COMM_SYS_Init(&vb)); sys_ready = 1;
 /* Set before calling init so partial VI/ISP initialization is unwound. */
 vi_ready = 1; TRY(SAMPLE_PLAT_VI_INIT(&vi));
 for (int p = 0; p < 2; ++p) {
  TRY(ov9281_isp_profile(p, 0, 0));
  ISP_PUB_ATTR_S pub; TRY(CVI_ISP_GetPubAttr(p, &pub));
  if (pub.f32FrameRate != 40) { fprintf(stderr, "uvc: ISP 40Hz setting did not apply\n"); return -1; }
 }
 block = CVI_VB_GetBlock(VB_INVALID_POOLID, layout.u32VBSize);
 if (block == VB_INVALID_HANDLE) return -1;
 composite.u32PoolId = CVI_VB_Handle2PoolId(block);
 VIDEO_FRAME_S *f = &composite.stVFrame;
 f->u32Width = TS_WIDTH; f->u32Height = TS_HEIGHT;
 f->enPixelFormat = PIXEL_FORMAT_YUV_PLANAR_420;
 f->enVideoFormat = VIDEO_FORMAT_LINEAR; f->enCompressMode = COMPRESS_MODE_NONE;
 f->enDynamicRange = DYNAMIC_RANGE_SDR8; f->enColorGamut = COLOR_GAMUT_BT709;
 f->u32Stride[0] = layout.u32MainStride;
 f->u32Stride[1] = f->u32Stride[2] = layout.u32CStride;
 f->u32Length[0] = layout.u32MainYSize;
 f->u32Length[1] = f->u32Length[2] = layout.u32MainCSize;
 f->u64PhyAddr[0] = CVI_VB_Handle2PhysAddr(block);
 f->u64PhyAddr[1] = f->u64PhyAddr[0] + ALIGN(layout.u32MainYSize, layout.u16AddrAlign);
 f->u64PhyAddr[2] = f->u64PhyAddr[1] + ALIGN(layout.u32MainCSize, layout.u16AddrAlign);
 mapped = CVI_SYS_MmapCache(f->u64PhyAddr[0], layout.u32VBSize);
 if (!mapped) return -1;
 memset(mapped, 128, layout.u32VBSize); /* Mono: neutral U and V for standard YUV420 JPEG. */
 for (int p = 0; p < 3; ++p) f->pu8VirAddr[p] = mapped + f->u64PhyAddr[p] - f->u64PhyAddr[0];
 VENC_CHN_ATTR_S attr = {0};
 attr.stVencAttr.enType = PT_MJPEG;
 attr.stVencAttr.u32MaxPicWidth = attr.stVencAttr.u32PicWidth = TS_WIDTH;
 attr.stVencAttr.u32MaxPicHeight = attr.stVencAttr.u32PicHeight = TS_HEIGHT;
 attr.stVencAttr.u32BufSize = 2 * TS_FRAME_MAX;
 attr.stVencAttr.bByFrame = CVI_TRUE;
 attr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
 attr.stRcAttr.stMjpegFixQp.u32SrcFrameRate = 40;
 attr.stRcAttr.stMjpegFixQp.fr32DstFrameRate = 40;
 attr.stRcAttr.stMjpegFixQp.u32Qfactor = src->quality;
 TRY(CVI_VENC_CreateChn(0, &attr)); venc_ready = 1;
 VENC_RECV_PIC_PARAM_S recv = { .s32RecvPicNum = -1 };
 TRY(CVI_VENC_StartRecvFrame(0, &recv)); receiving = 1;
 return 0;
}
static void camera_close(void) {
 if (receiving) CVI_VENC_StopRecvFrame(0);
 if (venc_ready) CVI_VENC_DestroyChn(0);
 if (mapped) CVI_SYS_Munmap(mapped, layout.u32VBSize);
 if (block != VB_INVALID_HANDLE) CVI_VB_ReleaseBlock(block);
 if (vi_ready) { SAMPLE_COMM_VI_DestroyIsp(&vi); SAMPLE_COMM_VI_DestroyVi(&vi); }
 if (sys_ready) SAMPLE_COMM_SYS_Exit();
 if (camera_lock >= 0) close(camera_lock);
}
static int encode(struct ts_frame *out) {
 composite.stVFrame.u64PTS = out->left_ns / 1000;
 composite.stVFrame.u32TimeRef = out->left_ref;
 TRY(CVI_SYS_IonFlushCache(composite.stVFrame.u64PhyAddr[0], mapped, layout.u32VBSize));
 TRY(CVI_VENC_SendFrame(0, &composite, 1000));
 VENC_PACK_S packs[16] = {0};
 VENC_STREAM_S stream = { .pstPack = packs, .u32PackCount = 16 };
 TRY(CVI_VENC_GetStream(0, &stream, 1000));
 size_t used = 0; int rc = 0;
 if (stream.u32PackCount > 16) rc = -1;
 for (unsigned i = 0; !rc && i < stream.u32PackCount; ++i) {
  if (packs[i].u32Offset > packs[i].u32Len || !packs[i].pu8Addr) { rc = -1; break; }
  size_t len = packs[i].u32Len - packs[i].u32Offset;
  if (len > sizeof(out->jpeg) - used) { rc = -1; break; }
  memcpy(out->jpeg + used, packs[i].pu8Addr + packs[i].u32Offset, len); used += len;
 }
 if (CVI_VENC_ReleaseStream(0, &stream)) rc = -1;
 out->size = used;
 if (rc || used < 4 || out->jpeg[0] != 0xff || out->jpeg[1] != 0xd8 ||
     out->jpeg[used - 2] != 0xff || out->jpeg[used - 1] != 0xd9) {
  fprintf(stderr, "uvc: encoder returned invalid/oversized JPEG (%zu bytes)\n", used); return -1;
 }
 return 0;
}
static void *camera_worker(void *arg) {
 struct ts_source *src = arg;
 VIDEO_FRAME_INFO_S frame[2] = {0}; int held[2] = {0};
 uint64_t prev[2] = {0}, seq = 0, drops = 0, first = 0, last = 0;
 unsigned warmup = 4, unmatched = 0;
 struct ts_frame *out = malloc(sizeof(*out));
 if (!out) goto fail;
 while (!atomic_load(&src->quit) && !ts_stopped) {
  if (atomic_load(&src->imu.error)) goto fail;
  for (int p = 0; p < 2; ++p) if (!held[p]) {
   int rc = CVI_VI_GetChnFrame(0, p, &frame[p], prev[p] ? 500 : 3000);
   if (atomic_load(&src->quit) || ts_stopped) { if (!rc) held[p] = 1; goto done; }
   if (rc) { fprintf(stderr, "uvc: VI %d timeout/error %#x\n", p, rc); goto fail; }
   held[p] = 1; VIDEO_FRAME_S *f = &frame[p].stVFrame;
   uint64_t now = ts_now();
   if (f->u32Width != 640 || f->u32Height != 400 || f->u32Stride[0] < 640 ||
       f->u64PTS <= prev[p] || f->u64PTS * 1000 > now + 10000000ULL ||
       now > f->u64PTS * 1000 + 3000000000ULL) goto fail;
   prev[p] = f->u64PTS;
  }
  int64_t skew = (int64_t)frame[1].stVFrame.u64PTS - (int64_t)frame[0].stVFrame.u64PTS;
  /* < half of the 25ms trigger period; never reuse the old 20fps/20ms tolerance. */
  if (skew > 10000 || skew < -10000) {
   int p = skew > 0 ? 0 : 1;
   if (CVI_VI_ReleaseChnFrame(0, p, &frame[p])) goto fail;
   held[p] = 0; ++drops;
   if (++unmatched > 40) goto fail;
   continue;
  }
  unmatched = 0;
  for (int p = 0; p < 2; ++p) {
   VIDEO_FRAME_S *f = &frame[p].stVFrame;
   unsigned len = f->u32Stride[0] * 400;
   const uint8_t *y = CVI_SYS_MmapCache(f->u64PhyAddr[0], len);
   if (!y) goto fail;
   if (CVI_SYS_IonInvalidateCache(f->u64PhyAddr[0], (void *)y, len)) {
    CVI_SYS_Munmap((void *)y, len); goto fail;
   }
   for (unsigned row = 0; row < 400; ++row)
    memcpy(mapped + row * layout.u32MainStride + p * 640, y + row * f->u32Stride[0], 640);
   CVI_SYS_Munmap((void *)y, len);
   if (CVI_VI_ReleaseChnFrame(0, p, &frame[p])) goto fail;
   held[p] = 0;
  }
  if (warmup) { --warmup; continue; }
  out->seq = ++seq; out->left_ns = frame[0].stVFrame.u64PTS * 1000;
  out->right_ns = frame[1].stVFrame.u64PTS * 1000;
  out->left_ref = frame[0].stVFrame.u32TimeRef; out->right_ref = frame[1].stVFrame.u32TimeRef;
  out->pair_drops = drops;
  if (!first) first = out->left_ns;
  last = out->left_ns;
  if (encode(out)) goto fail;
  pthread_mutex_lock(&src->lock); memcpy(&src->latest, out, offsetof(struct ts_frame, jpeg) + out->size);
  pthread_mutex_unlock(&src->lock);
  for (int p = 0; p < 2; ++p) if (ov9281_isp_update_black_level(p)) goto fail;
  if (seq == 1 || seq % 400 == 0)
   fprintf(stderr, "uvc: captured=%llu fps=%.3f JPEG=%zu pair_drops=%llu skew_us=%lld\n",
    (unsigned long long)seq, last > first ? (seq - 1) * 1e9 / (last - first) : 0,
    out->size, (unsigned long long)drops, (long long)skew);
 }
 goto done;
fail:
 if (!ts_stopped && !atomic_load(&src->quit)) { fprintf(stderr, "uvc: camera/encoder stream failed\n"); atomic_store(&src->error, 1); }
done:
 for (int p = 0; p < 2; ++p) if (held[p]) CVI_VI_ReleaseChnFrame(0, p, &frame[p]);
 free(out); return NULL;
}
int ts_source_open(struct ts_source *src) {
 pthread_mutex_init(&src->lock, NULL);
 /* Take the camera lock before acquiring the IMU or changing its rate. */
 if (camera_init(src)) return -1;
 if (ts_imu_open(&src->imu)) return -1;
 if (ts_setting(&src->imu, "fsin_enable", "1")) return -1;
 src->imu.fsin_started = 1;
 if (pthread_create(&src->thread, NULL, camera_worker, src)) return -1;
 src->thread_started = 1;
 return 0;
}
void ts_source_close(struct ts_source *src) {
 atomic_store(&src->quit, 1);
 if (src->thread_started) pthread_join(src->thread, NULL);
 if (src->imu.fd >= 0) ts_imu_close(&src->imu);
 camera_close(); pthread_mutex_destroy(&src->lock);
}
