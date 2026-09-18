/* TideSense MBR DATA growth. Only the known three-partition layout is accepted. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
static void die(const char *s) { perror(s); exit(1); }
static uint32_t le(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
int main(void) {
 unsigned char m[512], old[512]; uint64_t bytes;
 int fd=open("/dev/mmcblk0",O_RDWR|O_SYNC); if(fd<0)die("open disk");
 if(pread(fd,m,512,0)!=512)die("read MBR");
 if(ioctl(fd,BLKGETSIZE64,&bytes))die("disk size");
 if(m[510]!=0x55||m[511]!=0xaa||m[450]!=0x0c||m[466]!=0x83||m[482]!=0x83||
 le(m+454)!=2048||le(m+458)!=161920||le(m+470)!=165888||le(m+474)!=409600||
 le(m+486)!=575488||memcmp(m+494,(unsigned char[16]){0},16)) {
 fprintf(stderr,"Unexpected partition layout; refusing to modify disk\n");return 1;
 }
 uint64_t sectors=bytes/512; uint32_t start=le(m+486), count=le(m+490);
 if(sectors<=start||sectors-start>UINT32_MAX||count>sectors-start||!count) {
 fprintf(stderr,"Invalid disk/partition size\n"); return 1;
 }
 uint32_t target=sectors-start;
 if(count<target) {
  int b=open("/etc/tidesense-mbr.before-grow",O_WRONLY|O_CREAT|O_EXCL,0600);
  if(b<0)die("create MBR backup (existing backup requires inspection)");
  if(write(b,m,512)!=512||fsync(b))die("save MBR backup");
  close(b);
  memcpy(old,m,512);
  for(int i=0;i<4;i++)m[490+i]=(target>>(8*i))&255;
  if(pwrite(fd,m,512,0)!=512||fsync(fd))die("write MBR");
  if(pread(fd,old,512,0)!=512||memcmp(old,m,512))die("verify MBR");
 }
 struct blkpg_partition p={.start=(long long)start*512,.length=(long long)target*512,.pno=3};
 struct blkpg_ioctl_arg a={.op=BLKPG_RESIZE_PARTITION,.datalen=sizeof(p),.data=&p};
 if(ioctl(fd,BLKPG,&a))die("update kernel partition size; retry after reboot");
 printf("DATA partition: %u -> %u sectors\n",count,target);close(fd);return 0;
}
