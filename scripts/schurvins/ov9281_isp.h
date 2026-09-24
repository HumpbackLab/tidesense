/* OV9281 monochrome profile; call after ISP initialization, before FSIN. */
#ifndef TIDESENSE_OV9281_ISP_H
#define TIDESENSE_OV9281_ISP_H
#include "cvi_isp.h"
#include "cvi_ae.h"
#include "cvi_awb.h"

/* Covered-lens RAW12 medians, this board at 2 ms / 640x400.
 * Use sensor analog gain (Q10), not total ISO which includes ISP gain. */
static inline CVI_U16 ov9281_black_level(VI_PIPE pipe, CVI_U32 gain)
{
 static const CVI_U32 gains[] = {1024, 4096, 7936, 15872};
 const CVI_U16 levels[] = {132, 148, 168, pipe == 0 ? 180 : 176};
 if (gain <= gains[0]) return levels[0];
 for (unsigned i = 1; i < 4; ++i) {
  if (gain <= gains[i]) {
   CVI_U32 span = gains[i] - gains[i-1];
   return levels[i-1] + ((gain - gains[i-1]) *
    (levels[i] - levels[i-1]) + span/2) / span;
  }
 }
 return levels[3];
}

static inline int ov9281_isp_update_black_level(VI_PIPE pipe)
{
 ISP_EXP_INFO_S info;
 ISP_BLACK_LEVEL_ATTR_S blc;
 int ret = CVI_ISP_QueryExposureInfo(pipe, &info);
 if (ret) return ret;
 if (!info.u32AGain) return CVI_SUCCESS; /* AE not initialized yet. */
 CVI_U16 offset = ov9281_black_level(pipe, info.u32AGain);
 ret = CVI_ISP_GetBlackLevelAttr(pipe, &blc);
 if (ret) return ret;
 if (blc.Enable && blc.enOpType == OP_TYPE_MANUAL &&
     blc.stManual.OffsetR == offset && blc.stManual.OffsetGr == offset &&
     blc.stManual.OffsetGb == offset && blc.stManual.OffsetB == offset)
  return CVI_SUCCESS;
 blc.Enable = CVI_TRUE;
 blc.enOpType = OP_TYPE_MANUAL;
 blc.stManual.OffsetR = blc.stManual.OffsetGr = offset;
 blc.stManual.OffsetGb = blc.stManual.OffsetB = offset;
 return CVI_ISP_SetBlackLevelAttr(pipe, &blc);
}

/* sharp/tnr are separate switches for controlled diagnostic comparisons.
 * Production uses both off to avoid sharpening halos and temporal trails. */
static int ov9281_isp_profile(VI_PIPE pipe, int sharp, int tnr)
{
 int ret;
 #define ISP_TRY(call) do { ret = (call); if (ret != CVI_SUCCESS) return ret; } while (0)
 ISP_EXPOSURE_ATTR_S ae;
 ISP_TRY(CVI_ISP_GetExposureAttr(pipe, &ae));
 ae.bByPass = CVI_FALSE;
 /* Fixed exposure and 4x analog gain: no AE adaptation or exposure-centre drift.
  * The sensor quantizes 2000 us to a whole number of exposure lines. */
 ae.enOpType = OP_TYPE_MANUAL;
 ae.stManual.enGainType = AE_TYPE_GAIN;
 ae.stManual.enExpTimeOpType = OP_TYPE_MANUAL;
 ae.stManual.enAGainOpType = OP_TYPE_MANUAL;
 ae.stManual.enDGainOpType = OP_TYPE_MANUAL;
 ae.stManual.enISPDGainOpType = OP_TYPE_MANUAL;
 ae.stManual.u32ExpTime = 2000;
 ae.stManual.u32AGain = 4096;
 ae.stManual.u32DGain = 1024;
 ae.stManual.u32ISPDGain = 1024;
 ISP_TRY(CVI_ISP_SetExposureAttr(pipe, &ae));
 ISP_WB_ATTR_S wb;
 ISP_TRY(CVI_ISP_GetWBAttr(pipe, &wb));
 wb.bByPass = CVI_FALSE;
 wb.enOpType = OP_TYPE_MANUAL;
 wb.stManual.u16Rgain = wb.stManual.u16Grgain = 1024;
 wb.stManual.u16Gbgain = wb.stManual.u16Bgain = 1024;
 ISP_TRY(CVI_ISP_SetWBAttr(pipe, &wb));
 #define ISP_ENABLE(T, name, value) do { T attr; \
  ISP_TRY(CVI_ISP_Get##name##Attr(pipe, &attr)); attr.Enable = (value); \
  ISP_TRY(CVI_ISP_Set##name##Attr(pipe, &attr)); } while (0)
 ISP_ENABLE(ISP_DEMOSAIC_ATTR_S, Demosaic, CVI_FALSE);
 ISP_ENABLE(ISP_CCM_ATTR_S, CCM, CVI_FALSE);
 ISP_ENABLE(ISP_CNR_ATTR_S, CNR, CVI_FALSE);
 ISP_ENABLE(ISP_MONO_ATTR_S, Mono, CVI_TRUE);
 ISP_ENABLE(ISP_PRESHARPEN_ATTR_S, PreSharpen, sharp != 0);
 ISP_ENABLE(ISP_SHARPEN_ATTR_S, Sharpen, sharp != 0);
 ISP_ENABLE(ISP_TNR_ATTR_S, TNR, tnr != 0);
 ISP_COLOR_TONE_ATTR_S tone;
 ISP_TRY(CVI_ISP_GetColorToneAttr(pipe, &tone));
 tone.u16RedCastGain = tone.u16GreenCastGain = tone.u16BlueCastGain = 1024;
 ISP_TRY(CVI_ISP_SetColorToneAttr(pipe, &tone));
 /* Initialize at 4x; the capture loop verifies the applied analog gain. */
 ISP_BLACK_LEVEL_ATTR_S blc;
 ISP_TRY(CVI_ISP_GetBlackLevelAttr(pipe, &blc));
 blc.Enable = CVI_TRUE;
 blc.enOpType = OP_TYPE_MANUAL;
 blc.stManual.OffsetR = blc.stManual.OffsetGr = ov9281_black_level(pipe, 4096);
 blc.stManual.OffsetGb = blc.stManual.OffsetB = ov9281_black_level(pipe, 4096);
 ISP_TRY(CVI_ISP_SetBlackLevelAttr(pipe, &blc));
 /* SDK defaults enable YNR but set every ISO strength to zero.
  * 64 reduced wall temporal noise in the board comparison; 128 added little.
  * Retain the existing edge/coring/filter parameters and avoid temporal NR. */
 ISP_YNR_ATTR_S ynr;
 ISP_TRY(CVI_ISP_GetYNRAttr(pipe, &ynr));
 ynr.Enable = CVI_TRUE;
 ynr.enOpType = OP_TYPE_AUTO;
 ynr.TuningMode = YNR_RESULT;
 for (int i = 0; i < ISP_AUTO_ISO_STRENGTH_NUM; ++i)
  ynr.stAuto.NoiseSuppressStr[i] = 64;
 ISP_TRY(CVI_ISP_SetYNRAttr(pipe, &ynr));
 /* CV181x keeps RGB gamma enabled with its sRGB curve. GammaAttr.Enable
  * controls the additional Y gamma stage, whose SDK SDR curve otherwise
  * lifts already gamma-encoded monochrome output a second time. */
 ISP_ENABLE(ISP_DRC_ATTR_S, DRC, CVI_FALSE);
 ISP_ENABLE(ISP_GAMMA_ATTR_S, Gamma, CVI_FALSE);
 /* Keep Bayer spatial NR unchanged. */
 #undef ISP_ENABLE
 #undef ISP_TRY
 return CVI_SUCCESS;
}
#endif
