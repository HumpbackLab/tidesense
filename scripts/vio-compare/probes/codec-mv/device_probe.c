// Diagnostic wrapper around the SDK's file-input hardware encoder sample.
// Read only the encoder-owned H265 mvCol allocation, after GetStream completes.
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "cvi_venc.h"
extern uint64_t venc_mv_input_stamp(void);
extern void venc_mv_cache_frame(VENC_CHN, const VENC_STREAM_S *);

static unsigned frame_number;
static double sent_ms;
static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return 1000.0 * t.tv_sec + t.tv_nsec * 1e-6;
}

int __real_system(const char *command);
int __wrap_system(const char *command) {
  // Do not let legacy libsys alter global power/QoS registers for this probe.
  if (command && strncmp(command, "devmem ", 7) == 0) return 0;
  return __real_system(command);
}

static void save_text(const char *source, const char *directory, const char *name) {
  char path[1024], buffer[4096];
  snprintf(path, sizeof(path), "%s/%03u-%s.txt", directory, frame_number, name);
  FILE *in = fopen(source, "r"), *out = fopen(path, "w");
  if (in && out) {
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in))) fwrite(buffer, 1, n, out);
  }
  if (in) fclose(in);
  if (out) fclose(out);
}

static int signed12(unsigned value) { return value & 2048 ? (int)value - 4096 : (int)value; }

static void decode_ip_probe(const unsigned char *buffer, size_t size, const char *directory,
                            const VENC_STREAM_S *stream, unsigned width, unsigned height) {
  // Diagnostic layout established by known-motion inputs, NOT an SDK ABI.
  // Only the tested Normal-P, two-reconstruction-buffer configuration is supported.
  unsigned cols = (width + 63) / 64, rows = (height + 63) / 64;
  size_t stride = cols * rows * 128;
  unsigned slot = stream->u32Seq % 2;
  int is_p = 0;
  for (unsigned i = 0; i < stream->u32PackCount; ++i)
    is_p |= stream->pstPack[i].DataType.enH265EType == H265E_NALU_PSLICE;
  if (!is_p) return;  // I-frame contents and padding must never become vectors.
  if ((slot + 1) * stride > size) { fprintf(stderr, "MV_DECODE buffer too small\n"); return; }
  char path[1024];
  snprintf(path, sizeof(path), "%s/%03u-vectors.csv", directory, frame_number);
  FILE *out = fopen(path, "w");
  if (!out) { perror("open decoded vectors"); return; }
  fprintf(out, "frame,seq,slot,current_x,current_y,inter_flag,back_x_qpel,back_y_qpel,forward_dx_px,forward_dy_px\n");
  unsigned count = 0, inter = 0;
  for (unsigned cy = 0; cy < rows; ++cy) for (unsigned cx = 0; cx < cols; ++cx)
    for (unsigned r = 0; r < 4; ++r) {
      size_t offset = slot * stride + ((cy * cols + cx) * 4 + r) * 16;
      __uint128_t packed;
      memcpy(&packed, buffer + offset, 16);
      for (unsigned c = 0; c < 4; ++c) {
        unsigned x = cx * 64 + c * 16, y = cy * 64 + r * 16;
        if (x >= width || y >= height) continue;
        unsigned record = (unsigned)(packed >> (25 * c)) & 0x1ffffff;
        int vy = signed12(record & 4095), vx = signed12((record >> 12) & 4095);
        unsigned flag = (record >> 24) & 1;
        // flag=0 records are retained as diagnostics, never valid proposals.
        fprintf(out, "%u,%u,%u,%u,%u,%u,%d,%d,%.2f,%.2f\n", frame_number,
                stream->u32Seq, slot, x + 8, y + 8, flag, vx, vy, -vx * .25, -vy * .25);
        ++count; inter += flag;
      }
    }
  fclose(out);
  fprintf(stderr, "MV_DECODE frame=%u slot=%u cells=%u inter=%u\n", frame_number, slot, count, inter);
}

static void snapshot(const char *directory, const VENC_STREAM_S *stream, unsigned width, unsigned height) {
  const char *summary = "/sys/kernel/debug/ion/cvi_carveout_heap_dump/summary";
  char line[1024];
  FILE *in = fopen(summary, "r");
  if (!in) { perror("ion summary"); return; }
  save_text(summary, directory, "ion");
  save_text("/proc/cvitek/codec", directory, "codec");
  while (fgets(line, sizeof(line), in)) {
    unsigned heap, maps;
    unsigned long long size, physical;
    char name[128];
    if (sscanf(line, "%u %llu %llx %u %127s", &heap, &size, &physical, &maps, name) != 5) continue;
    if (!strstr(name, "VENC_") || !strstr(name, "mvCol")) continue;
    // This board's reserved ION carveout; never map registers or arbitrary RAM.
    if (!size || size > 4 * 1024 * 1024 || physical < 0x8b440000ULL || physical + size > 0x8ff40000ULL) {
      fprintf(stderr, "MV_SNAPSHOT rejected physical=%llx size=%llu\n", physical, size);
      continue;
    }
    long page = sysconf(_SC_PAGESIZE);
    off_t base = physical & ~((unsigned long long)page - 1);
    size_t offset = physical - base, length = offset + size;
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); continue; }
    void *map = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, base);
    if (map == MAP_FAILED) { perror("mmap mvCol"); close(fd); continue; }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%03u-%s.bin", directory, frame_number, name);
    FILE *out = fopen(path, "wb");
    if (out) {
      size_t written = fwrite((const char *)map + offset, 1, size, out);
      fclose(out);
      fprintf(stderr, "MV_SNAPSHOT frame=%u name=%s physical=%llx size=%llu written=%zu\n",
              frame_number, name, physical, size, written);
    } else perror("open snapshot output");
    decode_ip_probe((const unsigned char *)map + offset, size, directory, stream, width, height);
    munmap(map, length);
    close(fd);
  }
  fclose(in);
}

CVI_S32 __real_CVI_VENC_SendFrame(VENC_CHN, const VIDEO_FRAME_INFO_S *, CVI_S32);
CVI_S32 __wrap_CVI_VENC_SendFrame(VENC_CHN channel, const VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout) {
  sent_ms = now_ms();
  if (getenv("VENC_MV_FRAME_LIST")) {
    VIDEO_FRAME_INFO_S stamped = *frame;
    stamped.stVFrame.u64PTS = venc_mv_input_stamp();
    return __real_CVI_VENC_SendFrame(channel, &stamped, timeout);
  }
  return __real_CVI_VENC_SendFrame(channel, frame, timeout);
}

CVI_S32 __real_CVI_VENC_GetStream(VENC_CHN, VENC_STREAM_S *, CVI_S32);
CVI_S32 __wrap_CVI_VENC_GetStream(VENC_CHN channel, VENC_STREAM_S *stream, CVI_S32 timeout) {
  CVI_S32 result = __real_CVI_VENC_GetStream(channel, stream, timeout);
  double ready_ms = now_ms();
  if (result == CVI_SUCCESS) {
    venc_mv_cache_frame(channel, stream);
    const char *directory = getenv("VENC_MV_PROBE_DIR");
    uint64_t pts = stream->u32PackCount ? stream->pstPack[0].u64PTS : 0;
    fprintf(stderr, "ENCODED frame=%u seq=%u pts=%" PRIu64 " packs=%u send_to_stream_ms=%.3f\n",
            frame_number, stream->u32Seq, pts, stream->u32PackCount, ready_ms - sent_ms);
    if (directory) {
      VENC_CHN_ATTR_S attr;
      if (CVI_VENC_GetChnAttr(channel, &attr) == CVI_SUCCESS)
        snapshot(directory, stream, attr.stVencAttr.u32PicWidth, attr.stVencAttr.u32PicHeight);
    }
    fprintf(stderr, "SNAPSHOT_MS frame=%u ms=%.3f\n", frame_number, now_ms() - ready_ms);
    ++frame_number;
  }
  return result;
}
