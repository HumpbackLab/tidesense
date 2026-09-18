/* Exercise the real driver callbacks without touching sensor hardware. */
#include <assert.h>
#include <math.h>
#include "ov9281_cmos.c"

int main(void)
{
 ISP_SNS_STATE_S state = {0};
 const struct { int width, height; OV9281_MODE_E mode; int fps, vts; } modes[] = {
  {640, 400, OV9281_MODE_640X400P20, 20, 2730},
  {640, 400, OV9281_MODE_640X400P60, 60, 910},
  {640, 480, OV9281_MODE_640X480P60, 60, 910},
  {1280, 800, OV9281_MODE_1280X800P60, 60, 910},
 };
 g_pastOv9281[0] = &state;
 sensor_global_init(0);
 assert(state.u8ImgMode == OV9281_MODE_640X400P20);
 assert(state.u32FLStd == 2730);
 for (unsigned int i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
  ISP_CMOS_SENSOR_IMAGE_MODE_S image = {
   .u16Width = modes[i].width, .u16Height = modes[i].height, .f32Fps = modes[i].fps,
  };
  SNS_COMBO_DEV_ATTR_S rx;
  ISP_SNS_ISP_INFO_S isp = {0};
  AE_SENSOR_DEFAULT_S ae = {0};
  assert(cmos_set_image_mode(0, &image) == CVI_SUCCESS);
  assert(cmos_set_wdr_mode(0, WDR_MODE_NONE) == CVI_SUCCESS);
  assert(state.u8ImgMode == modes[i].mode);
  assert(sensor_rx_attr(0, &rx) == CVI_SUCCESS);
  assert(rx.img_size.width == (unsigned)modes[i].width && rx.img_size.height == (unsigned)modes[i].height);
  assert(cmos_get_wdr_size(0, &isp) == CVI_SUCCESS);
  assert(isp.img_size[0].stSnsSize.u32Width == (unsigned)modes[i].width);
  assert(isp.img_size[0].stSnsSize.u32Height == (unsigned)modes[i].height);
  assert(cmos_get_ae_default(0, &ae) == CVI_SUCCESS);
  assert(ae.u32FullLinesStd == (unsigned)modes[i].vts);
  assert(ae.u32MaxIntTime == (unsigned)modes[i].vts - 25);
  assert(ae.f32Fps == modes[i].fps);
  assert(cmos_fps_set(0, modes[i].fps / 2, &ae) == CVI_SUCCESS);
  assert(ae.u32FullLinesStd == (unsigned)modes[i].vts * 2);
  assert(ae.u32MaxIntTime == (unsigned)modes[i].vts * 2 - 25);
  assert(cmos_fps_set(0, modes[i].fps + 1, &ae) == CVI_FAILURE);
  assert(cmos_fps_set(0, modes[i].fps, &ae) == CVI_SUCCESS);
  g_au16Ov9281_HwSync[0] = 1;
  assert(cmos_get_ae_default(0, &ae) == CVI_SUCCESS);
  assert(ae.u32MaxIntTime == 546 && ae.u32MaxIntTimeTarget == 546);
  assert(cmos_fps_set(0, modes[i].fps, &ae) == CVI_SUCCESS);
  assert(ae.u32MaxIntTime == 546);
  assert(ae.u32LinesPer500ms == 27300 && ae.u32HmaxTimes == 18);
  ISP_SNS_REGS_INFO_S *regs = &state.astSyncInfo[0].snsCfg;
  assert(((regs->astI2cData[LINEAR_VTS_0].u32Data << 8) |
   regs->astI2cData[LINEAR_VTS_1].u32Data) == 910);
  CVI_U32 exposure = 65535;
  assert(cmos_inttime_update(0, &exposure) == CVI_SUCCESS);
  assert(((regs->astI2cData[LINEAR_EXP_0].u32Data << 12) |
   (regs->astI2cData[LINEAR_EXP_1].u32Data << 4) |
   (regs->astI2cData[LINEAR_EXP_2].u32Data >> 4)) == 546);
  g_au16Ov9281_HwSync[0] = 0;
  assert(cmos_fps_set(0, modes[i].fps, &ae) == CVI_SUCCESS);
  const float invalid[] = {0, -1, 61, NAN};
  for (unsigned int n = 0; n < sizeof(invalid) / sizeof(invalid[0]); n++) {
   image.f32Fps = invalid[n];
   assert(cmos_set_image_mode(0, &image) == CVI_FAILURE);
   assert(state.u8ImgMode == modes[i].mode);
  }
  image.f32Fps = modes[i].fps;
  image.u16Width = 800;
  assert(cmos_set_image_mode(0, &image) == CVI_FAILURE);
  printf("PASS mode %dx%d@%d: RX/ISP size, WDR preservation, AE full/half rate, FSIN timing/exposure clamp, rejected invalid modes\n",
   modes[i].width, modes[i].height, modes[i].fps);
 }
 return 0;
}
