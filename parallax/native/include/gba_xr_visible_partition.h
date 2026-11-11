#ifndef GBA_XR_VISIBLE_PARTITION_H
#define GBA_XR_VISIBLE_PARTITION_H

#include "gba_xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A conservative, zero-parallax-only partition of a resolved frame. Every
 * visible final pixel is placed in exactly one source bucket according to its
 * final provenance. This is not an extraction of hidden PPU candidates. */
typedef struct gbaxr_visible_partition {
  uint8_t rgba[GBAXR_MAX_LAYERS][GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4];
  uint8_t coverage[GBAXR_MAX_LAYERS][GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];
  uint32_t priority[GBAXR_MAX_LAYERS];
  uint8_t present[GBAXR_MAX_LAYERS];
} gbaxr_visible_partition;

/* Returns zero when every provenance source is a known GBA source. The output
 * is cleared before construction; on success, its nonzero coverage masks form
 * a disjoint, complete partition of the supplied resolved frame. */
int gbaxr_build_visible_partition(const uint8_t *resolved_rgba,
                                  const gbaxr_pixel_provenance *provenance,
                                  gbaxr_visible_partition *out_partition);

#ifdef __cplusplus
}
#endif

#endif
