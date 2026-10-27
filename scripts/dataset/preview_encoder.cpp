#include "preview_encoder.hpp"
#include <cvi_sys.h>
#include <cvi_vb.h>
#include <cvi_vpss.h>
#include <cvi_venc.h>
#include <cvi_buffer.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace {
void check(int result,const char* call) {
 if (!result) return;
 std::ostringstream out;out<<call<<" failed: 0x"<<std::hex<<unsigned(result);
 throw std::runtime_error(out.str());
}
}
struct PreviewEncoder::Impl {
 unsigned outWidth,outHeight;
 VB_POOL inputPool=VB_INVALID_POOLID,outputPool=VB_INVALID_POOLID;
 VB_BLK block=VB_INVALID_HANDLE;
 VPSS_GRP group=-1;
 int channel=-1;
 bool groupCreated=false,groupStarted=false,channelEnabled=false,poolAttached=false,receiving=false;
 VIDEO_FRAME_INFO_S input{};
 VB_CAL_CONFIG_S layout{};
 uint8_t* mapped=nullptr;
 Impl(unsigned width,unsigned height):outWidth(2*std::min(width,640u)),outHeight(height*std::min(width,640u)/width) {
  outHeight=(outHeight+1)&~1u;
  try {
   COMMON_GetPicBufferConfig(2*width,height,PIXEL_FORMAT_YUV_PLANAR_420,DATA_BITWIDTH_8,
                            COMPRESS_MODE_NONE,DEFAULT_ALIGN,&layout);
   VB_POOL_CONFIG_S pool{};pool.u32BlkSize=layout.u32VBSize;pool.u32BlkCnt=1;pool.enRemapMode=VB_REMAP_MODE_CACHED;
   inputPool=CVI_VB_CreatePool(&pool);
   if (inputPool==VB_INVALID_POOLID) throw std::runtime_error("Cannot allocate preview input pool");
   block=CVI_VB_GetBlock(inputPool,layout.u32VBSize);
   if (block==VB_INVALID_HANDLE) throw std::runtime_error("Cannot allocate preview input block");
   auto& f=input.stVFrame;
   input.u32PoolId=inputPool;f.u32Width=2*width;f.u32Height=height;
   f.enPixelFormat=PIXEL_FORMAT_YUV_PLANAR_420;f.enVideoFormat=VIDEO_FORMAT_LINEAR;
   f.enCompressMode=COMPRESS_MODE_NONE;f.enDynamicRange=DYNAMIC_RANGE_SDR8;f.enColorGamut=COLOR_GAMUT_BT709;
   f.u32Stride[0]=layout.u32MainStride;f.u32Stride[1]=f.u32Stride[2]=layout.u32CStride;
   f.u32Length[0]=layout.u32MainYSize;f.u32Length[1]=f.u32Length[2]=layout.u32MainCSize;
   f.u64PhyAddr[0]=CVI_VB_Handle2PhysAddr(block);
   f.u64PhyAddr[1]=f.u64PhyAddr[0]+ALIGN(layout.u32MainYSize,layout.u16AddrAlign);
   f.u64PhyAddr[2]=f.u64PhyAddr[1]+ALIGN(layout.u32MainCSize,layout.u16AddrAlign);
   mapped=static_cast<uint8_t*>(CVI_SYS_MmapCache(f.u64PhyAddr[0],layout.u32VBSize));
   if (!mapped) throw std::runtime_error("Cannot map preview input");
   std::memset(mapped,128,layout.u32VBSize);
   for(unsigned p=0;p<3;++p) f.pu8VirAddr[p]=mapped+(f.u64PhyAddr[p]-f.u64PhyAddr[0]);
   group=CVI_VPSS_GetAvailableGrp();
   if (group<0) throw std::runtime_error("No VPSS group available for preview");
   VPSS_GRP_ATTR_S ga{};ga.u32MaxW=2*width;ga.u32MaxH=height;
   ga.enPixelFormat=PIXEL_FORMAT_YUV_PLANAR_420;ga.stFrameRate={-1,-1};ga.u8VpssDev=0;
   check(CVI_VPSS_CreateGrp(group,&ga),"CVI_VPSS_CreateGrp");groupCreated=true;
   VPSS_CHN_ATTR_S ca{};ca.u32Width=outWidth;ca.u32Height=outHeight;
   ca.enVideoFormat=VIDEO_FORMAT_LINEAR;ca.enPixelFormat=PIXEL_FORMAT_YUV_PLANAR_420;
   ca.stFrameRate={-1,-1};ca.u32Depth=1;ca.stAspectRatio.enMode=ASPECT_RATIO_NONE;
   check(CVI_VPSS_SetChnAttr(group,0,&ca),"CVI_VPSS_SetChnAttr");
   pool.u32BlkSize=COMMON_GetPicBufferSize(outWidth,outHeight,PIXEL_FORMAT_YUV_PLANAR_420,
                          DATA_BITWIDTH_8,COMPRESS_MODE_NONE,DEFAULT_ALIGN);pool.u32BlkCnt=2;
   outputPool=CVI_VB_CreatePool(&pool);
   if (outputPool==VB_INVALID_POOLID) throw std::runtime_error("Cannot allocate preview output pool");
   check(CVI_VPSS_AttachVbPool(group,0,outputPool),"CVI_VPSS_AttachVbPool");poolAttached=true;
   check(CVI_VPSS_EnableChn(group,0),"CVI_VPSS_EnableChn");channelEnabled=true;
   check(CVI_VPSS_StartGrp(group),"CVI_VPSS_StartGrp");groupStarted=true;
   VENC_CHN_ATTR_S va{};auto& v=va.stVencAttr;
   v.enType=PT_JPEG;v.u32MaxPicWidth=v.u32PicWidth=outWidth;v.u32MaxPicHeight=v.u32PicHeight=outHeight;
   v.u32BufSize=outWidth*outHeight*2;v.bByFrame=CVI_TRUE;
   v.stAttrJpege.enReceiveMode=VENC_PIC_RECEIVE_SINGLE;
   va.stGopAttr.enGopMode=VENC_GOPMODE_NORMALP;
   // Never steal an encoder channel from another application.
   for(int c=0;c<VENC_MAX_CHN_NUM;++c) {
    VENC_CHN_ATTR_S existing{};
    if (CVI_VENC_GetChnAttr(c,&existing)==CVI_SUCCESS) continue;
    check(CVI_VENC_CreateChn(c,&va),"CVI_VENC_CreateChn");channel=c;break;
   }
   if(channel<0) throw std::runtime_error("No JPEG encoder channel available");
   VENC_JPEG_PARAM_S jp{};check(CVI_VENC_GetJpegParam(channel,&jp),"CVI_VENC_GetJpegParam");
   jp.u32Qfactor=80;check(CVI_VENC_SetJpegParam(channel,&jp),"CVI_VENC_SetJpegParam");
  } catch (...) {cleanup();throw;}
 }
 void cleanup() {
  if(receiving) CVI_VENC_StopRecvFrame(channel);
  if(channel>=0) CVI_VENC_DestroyChn(channel);
  if(groupStarted) CVI_VPSS_StopGrp(group);
  if(channelEnabled) CVI_VPSS_DisableChn(group,0);
  if(poolAttached) CVI_VPSS_DetachVbPool(group,0);
  if(groupCreated) CVI_VPSS_DestroyGrp(group);
  if(outputPool!=VB_INVALID_POOLID) CVI_VB_DestroyPool(outputPool);
  if(mapped) CVI_SYS_Munmap(mapped,layout.u32VBSize);
  if(block!=VB_INVALID_HANDLE) CVI_VB_ReleaseBlock(block);
  if(inputPool!=VB_INVALID_POOLID) CVI_VB_DestroyPool(inputPool);
 }
 ~Impl(){cleanup();}
 std::vector<uint8_t> encode(const LivePair& pair) {
  auto& f=input.stVFrame;
  if(pair.left.cols*2!=int(f.u32Width) || pair.left.rows!=int(f.u32Height))
   throw std::runtime_error("Preview capture size changed");
  for(int y=0;y<pair.left.rows;++y) {
   uint8_t* row=mapped+y*f.u32Stride[0];
   std::memcpy(row,pair.left.ptr(y),pair.left.cols);
   std::memcpy(row+pair.left.cols,pair.right.ptr(y),pair.right.cols);
  }
  f.u64PTS=pair.header.left_ns/1000;f.u32TimeRef=uint32_t(pair.header.sequence);
  check(CVI_SYS_IonFlushCache(f.u64PhyAddr[0],mapped,layout.u32VBSize),"CVI_SYS_IonFlushCache");
  VIDEO_FRAME_INFO_S scaled{};
  check(CVI_VPSS_SendFrame(group,&input,200),"CVI_VPSS_SendFrame");
  check(CVI_VPSS_GetChnFrame(group,0,&scaled,300),"CVI_VPSS_GetChnFrame");
  VENC_STREAM_S stream{};bool gotStream=false;
  std::vector<VENC_PACK_S> packs(16);stream.pstPack=packs.data();stream.u32PackCount=packs.size();
  try {
   VENC_RECV_PIC_PARAM_S receive{};receive.s32RecvPicNum=1;
   check(CVI_VENC_StartRecvFrame(channel,&receive),"CVI_VENC_StartRecvFrame");receiving=true;
   check(CVI_VENC_SendFrame(channel,&scaled,200),"CVI_VENC_SendFrame");
   check(CVI_VENC_GetStream(channel,&stream,500),"CVI_VENC_GetStream");gotStream=true;
   std::vector<uint8_t> jpeg;
   for(unsigned i=0;i<stream.u32PackCount;++i) {
    const auto& p=stream.pstPack[i];
    if(p.u32Offset>p.u32Len || !p.pu8Addr) throw std::runtime_error("Invalid JPEG packet");
    jpeg.insert(jpeg.end(),p.pu8Addr+p.u32Offset,p.pu8Addr+p.u32Len);
   }
   check(CVI_VENC_ReleaseStream(channel,&stream),"CVI_VENC_ReleaseStream");gotStream=false;
   check(CVI_VENC_StopRecvFrame(channel),"CVI_VENC_StopRecvFrame");receiving=false;
   check(CVI_VPSS_ReleaseChnFrame(group,0,&scaled),"CVI_VPSS_ReleaseChnFrame");
   if(jpeg.size()<4 || jpeg[0]!=0xff || jpeg[1]!=0xd8 || jpeg[jpeg.size()-2]!=0xff || jpeg.back()!=0xd9)
    throw std::runtime_error("Hardware JPEG returned invalid image");
   return jpeg;
  } catch(...) {
   if(gotStream) CVI_VENC_ReleaseStream(channel,&stream);
   if(receiving) {CVI_VENC_StopRecvFrame(channel);receiving=false;}
   CVI_VPSS_ReleaseChnFrame(group,0,&scaled);throw;
  }
 }
};
PreviewEncoder::PreviewEncoder(unsigned w,unsigned h):impl_(new Impl(w,h)){}
PreviewEncoder::~PreviewEncoder()=default;
unsigned PreviewEncoder::width()const{return impl_->outWidth;}
unsigned PreviewEncoder::height()const{return impl_->outHeight;}
std::vector<uint8_t> PreviewEncoder::encode(const LivePair& p){return impl_->encode(p);}
