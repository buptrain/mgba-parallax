#ifndef GBA_XR_GBA_BLEND_H
#define GBA_XR_GBA_BLEND_H

#include <stdint.h>

/* GBA BLDALPHA channel equation. EVA and EVB are clamped to hardware's 0..16
 * range; output is saturated at 255. */
uint8_t gbaxr_gba_alpha_channel(uint8_t target1, uint8_t target2, uint8_t eva, uint8_t evb);
void gbaxr_gba_alpha_rgba(const uint8_t target1[4], const uint8_t target2[4],
                          uint8_t eva, uint8_t evb, uint8_t out_rgba[4]);

#endif
