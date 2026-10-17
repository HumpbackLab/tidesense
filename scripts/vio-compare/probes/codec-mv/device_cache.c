// Offline cache collector for actual SG2002 H265 Normal-P collocated vectors.
// The cache feeds identical hardware proposals to PC and A53 VIO experiments.
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "cvi_venc.h"

static uint64_t input_stamp;
static double ms(void) {
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000.0 + t.tv_nsec * 1e-6;
}
static void fail(const char *message) { fprintf(stderr, "MV_CACHE_ERROR %s\n", message); exit(1); }
uint64_t venc_mv_input_stamp(void) { return input_stamp; }

int venc_mv_read_input(VIDEO_FRAME_S *frame) {
  static FILE *list;
  if (!list) list = fopen(getenv("VENC_MV_FRAME_LIST"), "r");
  if (!list) fail("cannot open frame list");
  unsigned long long stamp;
  char path[1024], magic[8];
  unsigned width, height, maximum;
  if (fscanf(list, "%llu %1023s", &stamp, path) != 2) fail("frame list exhausted");
  FILE *pgm = fopen(path, "rb");
  if (!pgm || fscanf(pgm, "%7s %u %u %u", magic, &width, &height, &maximum) != 4)
    fail("cannot read PGM header");
  if (strcmp(magic, "P5") || width != frame->u32Width || height != frame->u32Height || maximum != 255)
    fail("PGM geometry mismatch");
  fgetc(pgm);
  for (unsigned y = 0; y < height; ++y)
    if (fread(frame->pu8VirAddr[0] + y * frame->u32Stride[0], 1, width, pgm) != width)
      fail("truncated PGM");
  fclose(pgm);
  for (unsigned plane = 1; plane < 3; ++plane)
    memset(frame->pu8VirAddr[plane], 128, frame->u32Length[plane]);
  input_stamp = stamp;
  return CVI_SUCCESS;
}

struct Vector { float sx, sy, dx, dy, w, h; };
_Static_assert(sizeof(struct Vector) == 24, "cache vector layout");
static int s12(unsigned x) { return x & 2048 ? (int)x - 4096 : (int)x; }

void venc_mv_cache_frame(VENC_CHN channel, const VENC_STREAM_S *stream) {
  static FILE *output;
  static unsigned width, height, stride, index, parity;
  static uint64_t previous_stamp;
  static unsigned char *mapped, *previous;
  static struct Vector *vectors;
  const char *filename = getenv("VENC_MV_CACHE");
  if (!filename) return;
  double begin = ms();
  if (!output) {
    VENC_CHN_ATTR_S attr;
    if (CVI_VENC_GetChnAttr(channel, &attr) || attr.stVencAttr.enType != PT_H265)
      fail("cache requires H265");
    width = attr.stVencAttr.u32PicWidth; height = attr.stVencAttr.u32PicHeight;
    stride = ((width + 63) / 64) * ((height + 63) / 64) * 128;
    FILE *ion = fopen("/sys/kernel/debug/ion/cvi_carveout_heap_dump/summary", "r");
    if (!ion) fail("cannot read ION table");
    unsigned long long physical = 0, size = 0;
    char line[1024], name[128]; unsigned heap, maps;
    while (fgets(line, sizeof(line), ion)) {
      if (sscanf(line, "%u %llu %llx %u %127s", &heap, &size, &physical, &maps, name) == 5 &&
          strcmp(name, "VENC_0_H265_mvCol") == 0) break;
      physical = size = 0;
    }
    fclose(ion);
    if (physical < 0x8b440000ULL || physical + size > 0x8ff40000ULL || size < 2 * stride || size > 4*1024*1024)
      fail("invalid MV allocation");
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) fail("cannot open /dev/mem");
    long page = sysconf(_SC_PAGESIZE);
    off_t base = physical & ~((uint64_t)page - 1);
    size_t offset = physical - base;
    void *map = mmap(NULL, size + offset, PROT_READ, MAP_SHARED, fd, base);
    close(fd);
    if (map == MAP_FAILED) fail("cannot map MV buffer");
    mapped = (unsigned char *)map + offset;
    previous = malloc(2 * stride);
    vectors = malloc(((width + 15)/16) * ((height + 15)/16) * sizeof(*vectors));
    output = fopen(filename, "wb");
    if (!previous || !vectors || !output) fail("cache allocation/open failed");
    fwrite("CVMV0001", 1, 8, output);
  }
  if (stream->u32Seq != index || !stream->u32PackCount || stream->pstPack[0].u64PTS != input_stamp)
    fail("frame sequence/PTS mismatch");
  int is_p = 0;
  for (unsigned i = 0; i < stream->u32PackCount; ++i)
    is_p |= stream->pstPack[i].DataType.enH265EType == H265E_NALU_PSLICE;
  unsigned slot = (index + parity) % 2, changed = 0;
  if (index) {
    for (unsigned i = 0; i < 2; ++i)
      if (memcmp(mapped + i * stride, previous + i * stride, stride)) changed |= 1u << i;
    if (is_p && (changed == 1 || changed == 2)) {
      slot = changed == 1 ? 0 : 1;
      parity = slot ^ (index % 2);
    } else if (is_p && changed == 3) fail("both MV slots changed for one P frame");
  }
  unsigned count = 0, cols = (width + 63)/64, rows = (height + 63)/64;
  if (is_p) for (unsigned cy = 0; cy < rows; ++cy) for (unsigned cx = 0; cx < cols; ++cx)
    for (unsigned r = 0; r < 4; ++r) {
      __uint128_t packed;
      memcpy(&packed, mapped + slot*stride + ((cy*cols+cx)*4+r)*16, 16);
      for (unsigned c = 0; c < 4; ++c) {
        unsigned x = cx*64+c*16, y = cy*64+r*16;
        if (x >= width || y >= height) continue;
        unsigned word = (unsigned)(packed >> (25*c)) & 0x1ffffff;
        if (!(word & (1u << 24))) continue;
        float bx = s12((word >> 12)&4095)*.25f, by = s12(word&4095)*.25f;
        vectors[count++] = (struct Vector){x+8+bx, y+8+by, -bx, -by, 16, 16};
      }
    }
  memcpy(previous, mapped, 2*stride);
  double parsed = ms();
  fwrite(&input_stamp, 8, 1, output); fwrite(&previous_stamp, 8, 1, output);
  fwrite(&index, 4, 1, output); fwrite(&count, 4, 1, output);
  if (fwrite(vectors, sizeof(*vectors), count, output) != count) fail("cache write failed");
  if (fflush(output)) fail("cache flush failed");
  fprintf(stderr, "MV_CACHE seq=%u slot=%u changed=%u P=%d count=%u acquire_parse_ms=%.4f cache_write_ms=%.4f\n",
          index, slot, changed, is_p, count, parsed-begin, ms()-parsed);
  previous_stamp = input_stamp;
  ++index;
}
