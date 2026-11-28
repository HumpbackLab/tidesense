/* Capture synchronized mono8 and format one side-by-side MJPEG UVC frame. */
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
#include "tide_cam.h"
#include "ov9281_cmos_ex.h"
#include "../schurvins/ov9281_isp.h"

static SAMPLE_VI_CONFIG_S vi;
static tide_cam *camera;
static int camera_lock = -1, sys_ready, vi_ready, venc_ready, receiving;
static VB_BLK block = VB_INVALID_HANDLE;
static VB_POOL encoder_pool = VB_INVALID_POOLID;
static VIDEO_FRAME_INFO_S composite;
static VB_CAL_CONFIG_S layout;
static uint8_t *mapped;
static unsigned isp_fps;
#define TRY(call) do { int err = (call); if (err) { \
 fprintf(stderr, "uvc: %s failed (%#x)\n", #call, err); return -1; } } while (0)
#define READY_STEREO_FRAMES 24U

CVI_S32 __real_CVI_ISP_SetPubAttr(VI_PIPE p, const ISP_PUB_ATTR_S *a);
CVI_S32 __wrap_CVI_ISP_SetPubAttr(VI_PIPE p, const ISP_PUB_ATTR_S *a) {
 ISP_PUB_ATTR_S attr = *a;
 if (isp_fps) attr.f32FrameRate = isp_fps;
 return __real_CVI_ISP_SetPubAttr(p, &attr);
}
static int lock_camera(void) {
 camera_lock = open("/run/tidesense-camera.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
 if (camera_lock < 0 || flock(camera_lock, LOCK_EX | LOCK_NB)) {
  perror("camera ownership"); return -1;
 }
 return 0;
}

static int legacy_init(struct ts_source *src) {
 const struct ts_mode *mode = src->mode;
 unsigned width = mode->width, height = mode->height;
 isp_fps = mode->fps;
 SAMPLE_INI_CFG_S ini = {0};
 TRY(SAMPLE_COMM_VI_SetIniPath(src->ini)); TRY(SAMPLE_COMM_VI_ParseIni(&ini));
 if (ini.devNum != 2 || ini.u8HwSync[0] != 1 || ini.u8HwSync[1] != 1) {
  fprintf(stderr, "uvc: two FSIN-enabled OV9281 cameras required\n"); return -1;
 }
 for (int p = 0; p < 2; ++p) {
  if (ini.enSnsType[p] != OV_OV9281_MIPI_400P_60FPS_10BIT) {
   fprintf(stderr, "uvc: INI must select the board's OV9281 sensors\n");
   return -1;
  }
 }
 TRY(CVI_VI_SetDevNum(2)); TRY(SAMPLE_COMM_VI_IniToViCfg(&ini, &vi));
 for (int p = 0; p < 2; ++p) {
  vi.astViInfo[p].stChnInfo.enCompressMode = COMPRESS_MODE_NONE;
  vi.astViInfo[p].stChnInfo.enPixFormat = PIXEL_FORMAT_YUV_PLANAR_420;
 }
 VB_CONFIG_S vb = {0}; vb.u32MaxPoolCnt = 1;
 vb.astCommPool[0].u32BlkSize = COMMON_GetPicBufferSize(width, height,
  PIXEL_FORMAT_YUV_PLANAR_420, DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
 vb.astCommPool[0].u32BlkCnt = 12;
 TRY(SAMPLE_COMM_SYS_Init(&vb)); sys_ready = 1;
 /* Set before calling init so partial VI/ISP initialization is unwound. */
 vi_ready = 1; TRY(SAMPLE_PLAT_VI_INIT(&vi));
 for (int p = 0; p < 2; ++p) {
  TRY(ov9281_isp_profile(p, 0, 0));
  ISP_PUB_ATTR_S pub; TRY(CVI_ISP_GetPubAttr(p, &pub));
  if (pub.f32FrameRate != mode->fps) {
   fprintf(stderr, "uvc: ISP %uHz setting did not apply\n", mode->fps); return -1;
  }
 }
 return 0;
}
static int sensor_init(const struct ts_source *src) {
 tide_cam_config config;
 int rc = tide_cam_config_init(&config, sizeof(config));
 if (rc) return rc;
 config.mode.width = src->mode->width;
 config.mode.height = src->mode->height;
 config.sync = TIDE_CAM_SYNC_SENSOR_MASTER;
 config.master_camera_id = src->master_camera_id;
 config.fps = (tide_cam_rate){src->mode->fps, 1};
 /* Buffer encoder scheduling jitter. After a missed input, publish the
  * next matched pair with its original FSIN sequence. */
 config.queue = TIDE_CAM_FIFO_DROP_OLDEST;
 config.buffer_count = 8;
 char error[256];
 rc = tide_cam_open(&config, &camera, error, sizeof(error));
 if (rc) fprintf(stderr, "uvc: tide_cam open failed (%s): %s\n", tide_cam_status_string(rc), error);
 return rc;
}
static int encoder_init(const struct ts_source *src) {
 const struct ts_mode *mode = src->mode;
 unsigned width = mode->width, height = mode->height;
 COMMON_GetPicBufferConfig(width * 2, height, PIXEL_FORMAT_YUV_PLANAR_420,
  DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN, &layout);
 VB_POOL_CONFIG_S config = {.u32BlkSize = layout.u32VBSize, .u32BlkCnt = 1};
 encoder_pool = CVI_VB_CreatePool(&config);
 if (encoder_pool == VB_INVALID_POOLID) return -1;
 block = CVI_VB_GetBlock(encoder_pool, layout.u32VBSize);
 if (block == VB_INVALID_HANDLE) return -1;
 composite.u32PoolId = CVI_VB_Handle2PoolId(block);
 VIDEO_FRAME_S *f = &composite.stVFrame;
 f->u32Width = width * 2; f->u32Height = height;
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
 attr.stVencAttr.u32MaxPicWidth = attr.stVencAttr.u32PicWidth = width * 2;
 attr.stVencAttr.u32MaxPicHeight = attr.stVencAttr.u32PicHeight = height;
 attr.stVencAttr.u32BufSize = 2 * TS_FRAME_MAX;
 attr.stVencAttr.bByFrame = CVI_TRUE;
 attr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
 /* The kernel rejects destination rates above 120 even in FIXQP. Equal
  * rates disable VENC frame-rate conversion; the producer supplies cadence. */
 unsigned encoder_fps = mode->fps > 120 ? 120 : mode->fps;
 attr.stRcAttr.stMjpegFixQp.u32SrcFrameRate = encoder_fps;
 attr.stRcAttr.stMjpegFixQp.fr32DstFrameRate = encoder_fps;
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
 if (encoder_pool != VB_INVALID_POOLID) CVI_VB_DestroyPool(encoder_pool);
 if (vi_ready) { SAMPLE_COMM_VI_DestroyIsp(&vi); SAMPLE_COMM_VI_DestroyVi(&vi); }
 if (sys_ready) SAMPLE_COMM_SYS_Exit();
 if (camera && tide_cam_close(camera)) fprintf(stderr, "uvc: tide_cam close failed\n");
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
static void publish(struct ts_source *src, const struct ts_frame *frame) {
 pthread_mutex_lock(&src->lock);
 memcpy(&src->latest, frame, offsetof(struct ts_frame, jpeg) + frame->size);
 pthread_mutex_unlock(&src->lock);
}
static void camera_failure(struct ts_source *src) {
 if (!ts_stopped && !atomic_load(&src->quit)) {
  fprintf(stderr, "uvc: camera/encoder stream failed\n");
  atomic_store(&src->error, 1);
 }
}
static void *sensor_worker(void *arg) {
 struct ts_source *src = arg;
 struct ts_frame *out = calloc(1, sizeof(*out));
 if (!out) { camera_failure(src); return NULL; }
 uint64_t previous = 0, captured = 0, first = 0, start_ready = 0;
 unsigned consecutive = 0;
 while (!atomic_load(&src->quit) && !ts_stopped && !atomic_load(&src->imu.error)) {
  const tide_cam_frame_set *pair;
  int rc = tide_cam_acquire(camera, &pair, 200);
  if (rc == TIDE_CAM_TIMEOUT) continue;
  if (rc) {
   char error[256];
   tide_cam_get_error(camera, error, sizeof(error));
   fprintf(stderr, "uvc: tide_cam acquire failed (%s): %s\n", tide_cam_status_string(rc), error);
   goto fail;
  }
  unsigned width = src->mode->width, height = src->mode->height;
  for (unsigned p = 0; p < 2; ++p) {
   const tide_cam_frame *frame = &pair->frames[p];
   for (unsigned row = 0; row < height; ++row)
    memcpy(mapped + row * layout.u32MainStride + p * width,
           frame->data + (size_t)row * frame->stride, width);
  }
  out->seq = pair->fsin_sequence;
  out->fsin_ns = pair->fsin_timestamp_ns;
  out->exposure_ns = pair->exposure_ns;
  out->left_ns = pair->frames[0].timestamp_ns;
  out->right_ns = pair->frames[1].timestamp_ns;
  out->left_ref = pair->frames[0].sensor_sequence;
  out->right_ref = pair->frames[1].sensor_sequence;
  out->fps = src->mode->fps;
  if (tide_cam_release(camera, pair)) goto fail;
  if (previous && out->seq <= previous) goto fail;
  if (previous && out->seq > previous + 1) out->pair_drops += out->seq - previous - 1;
  if (out->seq == previous + 1 && consecutive) {
   ++consecutive;
  } else {
   start_ready = out->fsin_ns;
   consecutive = 1;
  }
  previous = out->seq;
  if (encode(out)) goto fail;
  publish(src, out);
  if (!atomic_load(&src->ready) && consecutive >= READY_STEREO_FRAMES) {
   uint64_t measured = (out->fsin_ns - start_ready) * out->fps;
   uint64_t expected = (consecutive - 1) * 1000000000ULL;
   if (measured >= expected * 97 / 100 && measured <= expected * 103 / 100)
    atomic_store(&src->ready, 1);
  }
  if (!first) first = out->fsin_ns;
  if (++captured == 1 || captured % 400 == 0)
   fprintf(stderr, "uvc: captured=%llu fps=%.3f JPEG=%zu pair_drops=%llu\n",
           (unsigned long long)captured,
           out->fsin_ns > first ? (captured - 1) * 1e9 / (out->fsin_ns - first) : 0,
           out->size, (unsigned long long)out->pair_drops);
 }
 free(out);
 return NULL;
fail:
 camera_failure(src);
 free(out);
 return NULL;
}
static void *legacy_worker(void *arg) {
 struct ts_source *src = arg;
 const struct ts_mode *mode = src->mode;
 unsigned width = mode->width, height = mode->height;
 VIDEO_FRAME_INFO_S frame[2] = {0}; int held[2] = {0};
 uint64_t prev[2] = {0}, seq = 0, drops = 0, first = 0, last = 0, captured = 0;
 unsigned warmup = 4, unmatched = 0;
 struct ts_frame *out = calloc(1, sizeof(*out));
 if (!out) goto fail;
 while (!atomic_load(&src->quit) && !ts_stopped) {
  if (atomic_load(&src->imu.error)) goto fail;
  for (int p = 0; p < 2; ++p) if (!held[p]) {
   int rc = CVI_VI_GetChnFrame(0, p, &frame[p], prev[p] ? 500 : 3000);
   if (atomic_load(&src->quit) || ts_stopped) { if (!rc) held[p] = 1; goto done; }
   if (rc) { fprintf(stderr, "uvc: VI %d timeout/error %#x\n", p, rc); goto fail; }
   held[p] = 1; VIDEO_FRAME_S *f = &frame[p].stVFrame;
   uint64_t now = ts_now();
   if (f->u32Width != width || f->u32Height != height || f->u32Stride[0] < width ||
       f->u64PTS <= prev[p] || f->u64PTS * 1000 > now + 10000000ULL ||
       now > f->u64PTS * 1000 + 3000000000ULL) {
    fprintf(stderr, "uvc: invalid VI %d frame dimensions or timestamp\n", p);
    goto fail;
   }
   prev[p] = f->u64PTS;
  }
  int64_t skew = (int64_t)prev[1] - (int64_t)prev[0];
  int discard = skew > 10000 ? 0 : skew < -10000 ? 1 : -1;
  if (discard >= 0) {
   if (CVI_VI_ReleaseChnFrame(0, discard, &frame[discard])) goto fail;
   held[discard] = 0; ++drops;
   if (++unmatched > 40) {
    fprintf(stderr, "uvc: could not pair VI frames, skew=%lld us\n", (long long)skew);
    goto fail;
   }
   continue;
  }
  unmatched = 0;
  for (int p = 0; p < 2; ++p) {
   VIDEO_FRAME_S *f = &frame[p].stVFrame;
   unsigned len = f->u32Stride[0] * height;
   const uint8_t *y = CVI_SYS_MmapCache(f->u64PhyAddr[0], len);
   if (!y) goto fail;
   if (CVI_SYS_IonInvalidateCache(f->u64PhyAddr[0], (void *)y, len)) {
    CVI_SYS_Munmap((void *)y, len); goto fail;
   }
   for (unsigned row = 0; row < height; ++row)
    memcpy(mapped + row * layout.u32MainStride + p * width, y + row * f->u32Stride[0], width);
   CVI_SYS_Munmap((void *)y, len);
   if (CVI_VI_ReleaseChnFrame(0, p, &frame[p])) goto fail;
   held[p] = 0;
  }
  if (warmup) { --warmup; continue; }
  ++seq;
  out->fps = mode->fps;
  out->seq = seq; out->left_ns = frame[0].stVFrame.u64PTS * 1000;
  out->right_ns = frame[1].stVFrame.u64PTS * 1000;
  out->left_ref = frame[0].stVFrame.u32TimeRef; out->right_ref = frame[1].stVFrame.u32TimeRef;
  out->pair_drops = drops;
  if (!first) first = out->left_ns;
  last = out->left_ns;
  if (encode(out)) goto fail;
  publish(src, out);
  for (int p = 0; p < 2; ++p) if (ov9281_isp_update_black_level(p)) goto fail;
  if (++captured == 1 || captured % 400 == 0)
   fprintf(stderr, "uvc: captured=%llu fps=%.3f JPEG=%zu pair_drops=%llu skew_us=%lld\n",
    (unsigned long long)captured, last > first ? (captured - 1) * 1e9 / (last - first) : 0,
    out->size, (unsigned long long)drops, (long long)skew);
 }
 goto done;
fail:
 camera_failure(src);
done:
 for (int p = 0; p < 2; ++p) if (held[p]) CVI_VI_ReleaseChnFrame(0, p, &frame[p]);
 free(out); return NULL;
}
int ts_source_open(struct ts_source *src) {
 pthread_mutex_init(&src->lock, NULL);
 int continuous = src->mode->fps != 40;
 if (continuous) {
  if (sensor_init(src) || ts_imu_open(&src->imu)) return -1;
 } else {
  if (lock_camera() || ts_imu_open(&src->imu) || legacy_init(src)) return -1;
  if (ts_setting(&src->imu, "fsin_enable", "1")) return -1;
  src->imu.fsin_started = 1;
 }
 if (encoder_init(src)) return -1;
 if (pthread_create(&src->thread, NULL, continuous ? sensor_worker : legacy_worker, src)) return -1;
 src->thread_started = 1;
 return 0;
}
int ts_source_wait_ready(struct ts_source *src) {
 uint64_t deadline = ts_now() + 6000000000ULL;
 while (!ts_stopped && !atomic_load(&src->error) && !atomic_load(&src->imu.error)) {
  if (atomic_load(&src->ready)) return 0;
  if (ts_now() >= deadline) {
   fprintf(stderr, "uvc: %s did not sustain synchronized stereo capture during startup\n", src->mode->name);
   return -1;
  }
  usleep(10000);
 }
 return -1;
}
void ts_source_close(struct ts_source *src) {
 atomic_store(&src->quit, 1);
 if (src->thread_started) pthread_join(src->thread, NULL);
 if (src->imu.fsin_started) {
  ts_setting(&src->imu, "fsin_enable", "0");
  src->imu.fsin_started = 0;
 }
 camera_close();
 if (src->imu.fd >= 0) ts_imu_close(&src->imu);
 if (camera_lock >= 0) close(camera_lock);
 pthread_mutex_destroy(&src->lock);
}
