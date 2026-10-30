#!/usr/bin/env python3
"""Extract the SDK's register construction into a host-only capture shim.

The generated code does not execute MMIO. C906 replays bounded register programs.
Keep the SDK's tiling and defaults rather than maintaining a second 640px driver.
"""
import argparse,re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[4]
hal=root/'osdrv/interdrv/ive/hal/cv181x'
s=(hal/'cvi_ive_platform.c').read_text();header=(hal/'cvi_ive_platform.h').read_text()
def masked(text):
 return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m[0]),text,flags=re.S)
def body(text,begin):
 clean=masked(text);op=clean.index('{',begin);depth=1;i=op+1
 while depth:
  depth+=(clean[i]=='{')-(clean[i]=='}');i+=1
 return op,i
def decl(text,tag):
 start=text.index(tag);_,end=body(text,start);return text[start:end+1]
def function(name):
 # A name may begin its own line after a split return type.
 clean=masked(s);match=re.search(r'(?m)^(?:static\s+)?(?:CVI_\w+\s+|void\s+|IVE_TOP_C\s*\*\s*)?'+re.escape(name)+r'\s*\(',clean)
 if not match:raise ValueError(name)
 start=match.start()
 if s[start:start+len(name)]==name:
  start=s.rfind('\n',0,start-1)+1
 op,end=body(s,start);return s[start:op].strip(),s[start:end]
names=['assign_ive_block_addr','init_ive_top_c','getImgFmtSel','ive_reset_reg','ive_set_int','ive_reset','ive_set_wh','setImgDst1','setImgSrc1','setRdmaEigval','emitTile','cvi_ive_Hist','_CVI_HW_STBoxFltAndEigCalc','_CVI_HW_STCandiCorner','cvi_ive_STCandiCorner']
functions=[function(name)for name in names]
# Encode the driver's retry-on-saturation as a C906 conditional, not a decision
# made from the host shim's synthetic register values.
proto,st=functions[-1];where=st.index('if (u16MaxEig == 65535)');_,end=body(st,where)
st=st[:where]+st[where:end].replace('if (u16MaxEig == 65535)','if (ive_emit_repeat_begin())',1)+'\nive_emit_repeat_end();'+st[end:]
functions[-1]=(proto,st)
parts=['/* Generated from SDK GPL-2.0-or-later IVE driver; do not edit. */',s[:s.index('*/')+2],
 (hal/'cvi_reg.h').read_text().split('#include <linux/io.h>')[0]+'\n#endif',
 '#define __iomem',decl(header,'enum filterop_mod'),decl(header,'enum dma_name')]
for tag in ['struct _IVE_ADDR_S','struct _IVE_MODE_S','struct _IVE_DEBUG_INFO_S','struct _IVE_IP_BLOCK_S']:parts.append(decl(header,tag))
parts+=['struct cvi_ive_device {int total_tile,tile_num,cur_optype,frame_done,op_done;};',
 'static struct _IVE_IP_BLOCK_S IVE_BLK_BA;static struct _IVE_DEBUG_INFO_S g_debug_info;static uintptr_t g_phy_shift;static bool g_dump_reg_info;',
 'static bool g_dump_image_info=false,g_dump_dma_info=false;static unsigned short g_u16MaxEig;static unsigned g_u32SizeS8C2;static IVE_ST_CANDI_CORNER_CTRL_S *g_pStCandiCornerCtrl;',
 'static IVE_TOP_C ive_top_c_l=_DEFINE_IVE_TOP_C;',
 '#define IVE_TOP_16_8_8(val,a,b,c) ((val)|((a)<<16)|((b)<<8)|(c))']
parts +=[proto+';'for proto,_ in functions]
parts.append('static CVI_S32 cvi_ive_go(struct cvi_ive_device*,IVE_TOP_C *top,CVI_BOOL instant,int mask,int kind){ive_emit_go(top,mask);return 0;}')
parts +=[text for _,text in functions]
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text('\n'.join(parts)+'\n')
