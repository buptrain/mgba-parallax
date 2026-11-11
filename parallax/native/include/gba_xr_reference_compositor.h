#ifndef GBA_XR_REFERENCE_COMPOSITOR_H
#define GBA_XR_REFERENCE_COMPOSITOR_H

#include <stdint.h>
#include "gba_xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gbaxr_blend_mode {
  GBAXR_REFERENCE_BLEND_OPAQUE = 0,
  GBAXR_REFERENCE_BLEND_ALPHA = 1
} gbaxr_blend_mode;

typedef struct gbaxr_composite_layer {
  gbaxr_image_view image;
  uint32_t priority; /* Lower values are in front, matching GBA priority convention. */
  uint32_t stable_order;
  gbaxr_blend_mode blend_mode;
} gbaxr_composite_layer;

/* Deterministic policy compositor for fixtures and zero-parallax validation.
 * Output buffer must be width * height * 4 bytes, tightly packed RGBA8.
 */
int gbaxr_reference_composite(const gbaxr_composite_layer *layers,
                              uint32_t layer_count,
                              uint32_t width,
                              uint32_t height,
                              uint8_t *out_rgba8);

#ifdef __cplusplus
}
#endif

#endif
