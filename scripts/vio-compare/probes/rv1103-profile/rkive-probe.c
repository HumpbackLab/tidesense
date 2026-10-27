#define _POSIX_C_SOURCE 200809L
#include "rk_mpi_ive.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int RVE_DRM_MmzAlloc(uint64_t*,void**,uint32_t);
int RVE_DRM_MmzFree(void*);
int RVE_DRM_MmzFlush_Start(void*);
int RVE_DRM_MmzFlush_End(void*);
static double now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec*1e-6; }
static IVE_MEM_INFO_S mem(uint32_t size) {
    uint64_t p=0;void *v=NULL;
    if (RVE_DRM_MmzAlloc(&p,&v,size)) { fprintf(stderr,"allocation failed\n");exit(2); }
    IVE_MEM_INFO_S m={.u64PhyAddr=p,.u64VirAddr=(uintptr_t)v,.u32Size=size};return m;
}
static int wait_done(int rc,IVE_HANDLE handle) {
    if (rc) return rc;
    bool done=false;return RK_MPI_IVE_Query(handle,&done,true);
}
int main(int argc,char **argv) {
    int w=argc>1?atoi(argv[1]):752,h=argc>2?atoi(argv[2]):480;
    setbuf(stdout,NULL);
    printf("init=%d\n",RK_MPI_IVE_Init());
    IVE_MEM_INFO_S src_mem[2],pyr_mem[2];
    IVE_IMAGE_S source[2],pyr[2][IVE_PYRAMID_MAX_LEVEL]={0};
    IVE_PYRAMID_CTRL_S controls[2]={0};
    int stride=(w+15)&~15;
    for (int i=0;i<2;i++) {
        src_mem[i]=mem(stride*h);
        source[i]=(IVE_IMAGE_S){.u32Width=w,.u32Height=h,.enType=IVE_IMAGE_TYPE_U8C1};
        source[i].au32Stride[0]=stride;source[i].au64PhyAddr[0]=src_mem[i].u64PhyAddr;source[i].au64VirAddr[0]=src_mem[i].u64VirAddr;
        RVE_DRM_MmzFlush_Start((void*)(uintptr_t)src_mem[i].u64VirAddr);
        unsigned char *data=(void*)(uintptr_t)src_mem[i].u64VirAddr;
        for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
            int xx=x-i;
            data[y*stride+x]=(unsigned char)((((xx/7)^(y/9))*53 + xx*3+y*2)&255);
        }
        RVE_DRM_MmzFlush_End(data);
        controls[i].level=3;
        int size=RK_MPI_IVE_Pyramid_GetSize(w,h,&controls[i]);
        printf("pyramid size=%d\n",size);
        if (size<=0) return 3;
        pyr_mem[i]=mem(size);controls[i].stPyramidMem=pyr_mem[i];
        IVE_HANDLE handle=-1;
        double begin=now();
        int rc=RK_MPI_IVE_Pyramid_Create(&handle,&source[i],pyr[i],&controls[i],true);
        rc=wait_done(rc,handle);
        printf("pyramid[%d] rc=%d handle=%d ms=%.3f\n",i,rc,handle,now()-begin);
        if (rc) return 4;
        RVE_DRM_MmzFlush_Start((void*)(uintptr_t)pyr_mem[i].u64VirAddr);
        for (int l=0;l<=3;l++) printf(" level=%d %ux%u stride=%u sample=%u\n",l,pyr[i][l].u32Width,pyr[i][l].u32Height,pyr[i][l].au32Stride[0],*(unsigned char*)(uintptr_t)pyr[i][l].au64VirAddr[0]);
        RVE_DRM_MmzFlush_End((void*)(uintptr_t)pyr_mem[i].u64VirAddr);
    }
    IVE_MEM_INFO_S prev=mem(500*8),next=mem(500*8),status=mem(512),error=mem(1024);
    IVE_POINT_S25Q7_S *p=(void*)(uintptr_t)prev.u64VirAddr;
    IVE_POINT_S25Q7_S *n=(void*)(uintptr_t)next.u64VirAddr;
    RVE_DRM_MmzFlush_Start(p);RVE_DRM_MmzFlush_Start(n);
    for(int i=0;i<200;i++) {p[i].s25q7X=(30+(i%20)*(w-60)/20)*128;p[i].s25q7Y=(30+(i/20)*(h-60)/10)*128;n[i]=p[i];}
    RVE_DRM_MmzFlush_End(p);RVE_DRM_MmzFlush_End(n);
    IVE_LK_OPTICAL_FLOW_PYR_CTRL_S lk={.enOutMode=IVE_LK_OPTICAL_FLOW_PYR_OUT_MODE_BOTH,.bUseInitFlow=1,.u16PtsNum=200,.u8MaxLevel=3,.u0q8MinEigThr=1,.u8IterCnt=20,.u0q11Eps=1};
    IVE_HANDLE handle=-1;double begin=now();
    int rc=RK_MPI_IVE_LKOpticalFlowPyr(&handle,pyr[0],pyr[1],&prev,&next,&status,&error,&lk,true);
    rc=wait_done(rc,handle);
    printf("LK rc=%d handle=%d ms=%.3f\n",rc,handle,now()-begin);
    if(rc)return 5;
    RVE_DRM_MmzFlush_Start(n);RVE_DRM_MmzFlush_Start((void*)(uintptr_t)status.u64VirAddr);
    unsigned char *s=(void*)(uintptr_t)status.u64VirAddr;
    int good=0;double dx=0,dy=0;
    for(int i=0;i<200;i++)if(s[i]){good++;dx+=(n[i].s25q7X-p[i].s25q7X)/128.0;dy+=(n[i].s25q7Y-p[i].s25q7Y)/128.0;}
    printf("good=%d mean_dx=%.6f mean_dy=%.6f expected_dx=1 expected_dy=0\n",good,dx/(good?good:1),dy/(good?good:1));
    RVE_DRM_MmzFlush_End(n);RVE_DRM_MmzFlush_End(s);
    RK_MPI_IVE_Deinit();
    RVE_DRM_MmzFree(p);RVE_DRM_MmzFree(n);RVE_DRM_MmzFree(s);RVE_DRM_MmzFree((void*)(uintptr_t)error.u64VirAddr);
    for(int i=0;i<2;i++){RVE_DRM_MmzFree((void*)(uintptr_t)src_mem[i].u64VirAddr);RVE_DRM_MmzFree((void*)(uintptr_t)pyr_mem[i].u64VirAddr);}
    return good>=150 && fabs(dx/good-1)<0.25 && fabs(dy/good)<0.25?0:6;
}
