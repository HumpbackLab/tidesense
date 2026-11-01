/* Exercise the real SG2002 encoder adapter with SDK calls substituted at link time. */
#include "jpeg.h"
#include <cvi_venc.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int channels[2], receiving[2], held[2], single_es, fail_right;
static VIDEO_FRAME_INFO_S input[2];
static uint8_t bytes[2][8] = {{0,0xff,0xd8,1,0xff,0xd9}, {0,0xff,0xd8,2,0xff,0xd9}};
CVI_S32 CVI_VENC_GetModParam(VENC_PARAM_MOD_S *p) {p->stJpegeModParam.bSingleEsBuf=single_es;return 0;}
CVI_S32 CVI_VENC_GetChnAttr(VENC_CHN c,VENC_CHN_ATTR_S *a) {(void)a;return c<2 && channels[c]?0:-1;}
CVI_S32 CVI_VENC_CreateChn(VENC_CHN c,const VENC_CHN_ATTR_S *a) {
 assert(c<2 && !channels[c] && a->stVencAttr.enType==PT_JPEG);
 assert(a->stVencAttr.u32PicWidth==640 && a->stVencAttr.u32PicHeight==400);
 channels[c]=1;return 0;
}
CVI_S32 CVI_VENC_DestroyChn(VENC_CHN c) {assert(!held[c] && !receiving[c]);channels[c]=0;return 0;}
CVI_S32 CVI_VENC_GetJpegParam(VENC_CHN c,VENC_JPEG_PARAM_S *p) {(void)c;memset(p,0,sizeof(*p));return 0;}
CVI_S32 CVI_VENC_SetJpegParam(VENC_CHN c,const VENC_JPEG_PARAM_S *p) {(void)c;assert(p->u32Qfactor==80);return 0;}
CVI_S32 CVI_VENC_StartRecvFrame(VENC_CHN c,const VENC_RECV_PIC_PARAM_S *p) {assert(!held[c] && p->s32RecvPicNum==1);receiving[c]=1;return 0;}
CVI_S32 CVI_VENC_StopRecvFrame(VENC_CHN c) {receiving[c]=0;return 0;}
CVI_S32 CVI_VENC_SendFrame(VENC_CHN c,const VIDEO_FRAME_INFO_S *f,CVI_S32 timeout) {
 (void)timeout;
 /* Exact original VI descriptor and all physical planes/strides go to VENC. */
 assert(f==&input[c] && f->stVFrame.u64PhyAddr[0]==100u+(unsigned)c);
 assert(f->stVFrame.u64PhyAddr[1]==200u+(unsigned)c && f->stVFrame.u64PhyAddr[2]==300u+(unsigned)c);
 return 0;
}
CVI_S32 CVI_VENC_GetStream(VENC_CHN c,VENC_STREAM_S *s,CVI_S32 timeout) {
 (void)timeout;
 if(c==1 && fail_right) return -1;
 assert(s->u32PackCount==1);
 held[c]=1;memset(s->pstPack,0,sizeof(*s->pstPack));
 s->pstPack->pu8Addr=bytes[c];s->pstPack->u32Offset=1;s->pstPack->u32Len=6;return 0;
}
CVI_S32 CVI_VENC_ReleaseStream(VENC_CHN c,VENC_STREAM_S *s) {assert(held[c] && s->pstPack->pu8Addr==bytes[c]);held[c]=0;return 0;}
int main(void) {
 tide_cam_config config={0};config.mode.width=640;config.mode.height=400;config.jpeg_quality=80;
 tide_jpeg *j=NULL;char error[256];tide_cam_jpeg_pair pair={0};
 single_es=1;assert(tide_jpeg_open(&config,&j,error)==TIDE_CAM_UNSUPPORTED && !j);
 single_es=0;assert(!tide_jpeg_open(&config,&j,error));
 for(unsigned p=0;p<2;++p) {
  input[p].stVFrame.u32Width=640;input[p].stVFrame.u32Height=400;
  input[p].stVFrame.u64PTS=1000000+p;
  for(unsigned plane=0;plane<3;++plane) input[p].stVFrame.u64PhyAddr[plane]=100*(plane+1)+p;
 }
 assert(!tide_jpeg_encode(j,input,&pair,error));
 for(unsigned p=0;p<2;++p) {
  assert(pair.frames[p].data==bytes[p]+1 && pair.frames[p].size==5); /* Direct SDK view, no JPEG copy. */
  assert(pair.frames[p].timestamp_ns==(1000000u+p)*1000ULL && pair.frames[p].camera_id==p);
 }
 assert(!tide_jpeg_release(j,error));
 fail_right=1;assert(tide_jpeg_encode(j,input,&pair,error)==TIDE_CAM_IO);
 assert(!held[0] && !held[1] && !receiving[0] && !receiving[1]);
 tide_jpeg_close(j);assert(!channels[0] && !channels[1]);
 puts("SG2002 JPEG adapter: original VI descriptors, borrowed contiguous output, paired failure cleanup passed");
 return 0;
}
