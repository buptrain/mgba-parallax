#include "gba_xr_gba_blend.h"

uint8_t gbaxr_gba_alpha_channel(uint8_t target1, uint8_t target2, uint8_t eva, uint8_t evb) {
  uint32_t result;
  if (eva > 16U) eva = 16U;
  if (evb > 16U) evb = 16U;
  result = ((uint32_t)target1 * eva + (uint32_t)target2 * evb) / 16U;
  return (uint8_t)(result > 255U ? 255U : result);
}

void gbaxr_gba_alpha_rgba(const uint8_t target1[4], const uint8_t target2[4],
                          uint8_t eva, uint8_t evb, uint8_t out_rgba[4]) {
  out_rgba[0] = gbaxr_gba_alpha_channel(target1[0], target2[0], eva, evb);
  out_rgba[1] = gbaxr_gba_alpha_channel(target1[1], target2[1], eva, evb);
  out_rgba[2] = gbaxr_gba_alpha_channel(target1[2], target2[2], eva, evb);
  out_rgba[3] = 255U;
}
