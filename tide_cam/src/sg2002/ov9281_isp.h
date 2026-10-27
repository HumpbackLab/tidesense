/* SG2002 OV9281 monochrome profile; initialized from the existing TideSense
 * scripts/schurvins/ov9281_isp.h profile. Kept private to this backend so the
 * library has no build/runtime dependency on SchurVINS. Call before FSIN. */
#ifndef TIDESENSE_OV9281_ISP_H
#define TIDESENSE_OV9281_ISP_H
#include "cvi_isp.h"
#include "cvi_ae.h"
#include "cvi_awb.h"

#define OV9281_EXPOSURE_MAX_US 500U
#ifndef OV9281_YNR_STRENGTH
#define OV9281_YNR_STRENGTH 192U
#endif
#if OV9281_YNR_STRENGTH < 0 || OV9281_YNR_STRENGTH > 255
#error "OV9281_YNR_STRENGTH must be in 0..255"
#endif

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
 /* AE adjusts exposure and gain; cap exposure at 0.5 ms.
  * The sensor quantizes the requested time to whole exposure lines. */
 ae.enOpType = OP_TYPE_AUTO;
 ae.stAuto.u8Compensation = 48;
 ae.stAuto.stExpTimeRange.u32Max = OV9281_EXPOSURE_MAX_US;
 if (ae.stAuto.stExpTimeRange.u32Min > OV9281_EXPOSURE_MAX_US)
  ae.stAuto.stExpTimeRange.u32Min = OV9281_EXPOSURE_MAX_US;
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
 ISP_ENABLE(ISP_RGBCAC_ATTR_S, RGBCAC, CVI_FALSE);
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
 /* Start from 4x; the capture loop updates the level as AE changes analog gain. */
 ISP_BLACK_LEVEL_ATTR_S blc;
 ISP_TRY(CVI_ISP_GetBlackLevelAttr(pipe, &blc));
 blc.Enable = CVI_TRUE;
 blc.enOpType = OP_TYPE_MANUAL;
 blc.stManual.OffsetR = blc.stManual.OffsetGr = ov9281_black_level(pipe, 4096);
 blc.stManual.OffsetGb = blc.stManual.OffsetB = ov9281_black_level(pipe, 4096);
 ISP_TRY(CVI_ISP_SetBlackLevelAttr(pipe, &blc));
 /* At 0.5 ms, the board static comparison found 192 reduced flat-region
  * temporal noise by about 40-49% while retaining strong edges/corners.
  * 255 reduced more noise but retained fewer total KLT features.
  * Keep existing ISO-dependent coring/filter tables; temporal NR stays off.
  * Build with OV9281_YNR_STRENGTH=64 to restore the previous profile. */
 ISP_YNR_ATTR_S ynr;
 ISP_TRY(CVI_ISP_GetYNRAttr(pipe, &ynr));
 ynr.Enable = CVI_TRUE;
 ynr.enOpType = OP_TYPE_AUTO;
 ynr.TuningMode = YNR_RESULT;
 for (int i = 0; i < ISP_AUTO_ISO_STRENGTH_NUM; ++i)
  ynr.stAuto.NoiseSuppressStr[i] = OV9281_YNR_STRENGTH;
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
