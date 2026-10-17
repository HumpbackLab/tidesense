// Standalone read-only snapshots of encoder-owned H264 allocations after GetStream.
// No assumed MV layout; these dumps are diagnostic, not valid feature proposals.
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
static unsigned frame_number;
static double sent_ms;
static double now_ms(void) {
  struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  return t.tv_sec*1000.0+t.tv_nsec*1e-6;
}
int __real_system(const char *command);
int __wrap_system(const char *command) {
  if (command && !strncmp(command,"devmem ",7)) return 0;
  return __real_system(command);
}
static void snapshot(VENC_CHN channel,const char *directory) {
  VENC_CHN_ATTR_S attr;
  if (CVI_VENC_GetChnAttr(channel,&attr) || attr.stVencAttr.enType!=PT_H264) exit(3);
  const char *summary="/sys/kernel/debug/ion/cvi_carveout_heap_dump/summary";
  FILE *in=fopen(summary,"r");
  if (!in) { perror("ion summary"); exit(4); }
  char path[1024],line[1024];
  snprintf(path,sizeof(path),"%s/%03u-ion.txt",directory,frame_number);
  FILE *list=fopen(path,"w");
  if (!list) exit(5);
  unsigned n=0;
  while (fgets(line,sizeof(line),in)) {
    fputs(line,list);
    unsigned heap,maps; unsigned long long size,physical; char name[128];
    if (sscanf(line,"%u %llu %llx %u %127s",&heap,&size,&physical,&maps,name)!=5) continue;
    // Only this idle, single-channel experiment's data allocations; no code or registers.
    if (strcmp(name,"VENC_0_H264_WorkBuffer") && strcmp(name,"VENC_0_ReconFrameBuf")) continue;
    if (!size || size>4*1024*1024 || physical<0x8b440000ULL || physical+size>0x8ff40000ULL) exit(6);
    long page=sysconf(_SC_PAGESIZE);
    off_t base=physical&~((unsigned long long)page-1);
    size_t offset=physical-base,length=offset+size;
    int fd=open("/dev/mem",O_RDONLY|O_SYNC);
    if (fd<0) { perror("open /dev/mem"); exit(7); }
    void *map=mmap(NULL,length,PROT_READ,MAP_SHARED,fd,base);
    if (map==MAP_FAILED) { perror("mmap"); exit(8); }
    snprintf(path,sizeof(path),"%s/%03u-%s-%llx.bin",directory,frame_number,name,physical);
    FILE *out=fopen(path,"wb");
    if (!out || fwrite((char*)map+offset,1,size,out)!=size) exit(9);
    fclose(out); munmap(map,length); close(fd); ++n;
    fprintf(stderr,"H264_BUFFER frame=%u name=%s physical=%llx bytes=%llu\n",frame_number,name,physical,size);
  }
  fclose(in); fclose(list);
  if (n!=3) { fprintf(stderr,"Expected work buffer and two recon buffers; got %u\n",n); exit(10); }
}
static void context(const VENC_STREAM_S *stream,const char *filename) {
  static FILE *csv;
  static const unsigned char *work;
  if (!csv) {
    FILE *ion=fopen("/sys/kernel/debug/ion/cvi_carveout_heap_dump/summary","r");
    if (!ion) exit(11);
    char line[1024],name[128]; unsigned heap,maps; unsigned long long size=0,physical=0;
    while (fgets(line,sizeof(line),ion)) {
      if (sscanf(line,"%u %llu %llx %u %127s",&heap,&size,&physical,&maps,name)==5 &&
          !strcmp(name,"VENC_0_H264_WorkBuffer")) break;
      physical=0;
    }
    fclose(ion);
    if (!physical || size<0x410 || size>4*1024*1024 || physical<0x8b440000ULL || physical+size>0x8ff40000ULL) exit(12);
    long page=sysconf(_SC_PAGESIZE);
    off_t base=physical&~((unsigned long long)page-1);
    size_t offset=physical-base;
    int fd=open("/dev/mem",O_RDONLY|O_SYNC);
    if (fd<0) exit(13);
    void *map=mmap(NULL,offset+size,PROT_READ,MAP_SHARED,fd,base);
    close(fd);
    if (map==MAP_FAILED) exit(14);
    work=(const unsigned char*)map+offset;
    csv=fopen(filename,"w");
    if (!csv) exit(15);
    fprintf(csv,"frame,seq,is_P,candidate,offset,back_x_qpel,back_y_qpel,forward_dx_px,forward_dy_px,read_ms\n");
  }
  double start=now_ms();
  uint32_t words[4];
  memcpy(words,work+0x400,sizeof(words));
  double read_ms=now_ms()-start;
  int is_p=0;
  for (unsigned k=0;k<stream->u32PackCount;++k)
    is_p|=stream->pstPack[k].DataType.enH264EType==H264E_NALU_PSLICE;
  for (unsigned k=0;k<4;++k) {
    int x=(int16_t)(words[k]>>16),y=(int16_t)words[k];
    fprintf(csv,"%u,%u,%d,%u,%u,%d,%d,%.2f,%.2f,%.6f\n",frame_number,stream->u32Seq,
      is_p,k,0x400+k*4,x,y,-x*.25,-y*.25,read_ms);
  }
}
CVI_S32 __real_CVI_VENC_SendFrame(VENC_CHN,const VIDEO_FRAME_INFO_S*,CVI_S32);
CVI_S32 __wrap_CVI_VENC_SendFrame(VENC_CHN ch,const VIDEO_FRAME_INFO_S *frame,CVI_S32 timeout) {
  sent_ms=now_ms();
  if (getenv("VENC_MV_FRAME_LIST")) {
    VIDEO_FRAME_INFO_S copy=*frame;
    copy.stVFrame.u64PTS=venc_mv_input_stamp();
    return __real_CVI_VENC_SendFrame(ch,&copy,timeout);
  }
  return __real_CVI_VENC_SendFrame(ch,frame,timeout);
}
CVI_S32 __real_CVI_VENC_GetStream(VENC_CHN,VENC_STREAM_S*,CVI_S32);
CVI_S32 __wrap_CVI_VENC_GetStream(VENC_CHN ch,VENC_STREAM_S *stream,CVI_S32 timeout) {
  CVI_S32 result=__real_CVI_VENC_GetStream(ch,stream,timeout);
  if (result==CVI_SUCCESS) {
    fprintf(stderr,"H264_ENCODED frame=%u seq=%u send_to_stream_ms=%.3f\n",frame_number,stream->u32Seq,now_ms()-sent_ms);
    const char *dir=getenv("VENC_H264_PROBE_DIR");
    if (dir) snapshot(ch,dir);
    const char *csv=getenv("VENC_H264_CONTEXT_CSV");
    if (csv) context(stream,csv);
    ++frame_number;
  }
  return result;
}
