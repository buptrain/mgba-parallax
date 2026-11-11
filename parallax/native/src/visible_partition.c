#include "gba_xr_visible_partition.h"

#include <stddef.h>
#include <string.h>

#define GBAXR_VISIBLE_PARTITION_PRIORITY_MIXED UINT32_MAX

int gbaxr_build_visible_partition(const uint8_t *resolved_rgba,
                                  const gbaxr_pixel_provenance *provenance,
                                  gbaxr_visible_partition *out_partition) {
  uint32_t pixel;
  if (resolved_rgba == NULL || provenance == NULL || out_partition == NULL) {
    return -1;
  }

  memset(out_partition, 0, sizeof(*out_partition));
  for (pixel = 0U; pixel < GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT; ++pixel) {
    uint32_t source = provenance[pixel].source;
    uint32_t rgba_offset = pixel * 4U;
    if (source >= GBAXR_MAX_LAYERS) {
      memset(out_partition, 0, sizeof(*out_partition));
      return -1;
    }
    if (!out_partition->present[source]) {
      out_partition->present[source] = 1U;
      out_partition->priority[source] = provenance[pixel].priority;
    } else if (out_partition->priority[source] != provenance[pixel].priority) {
      out_partition->priority[source] = GBAXR_VISIBLE_PARTITION_PRIORITY_MIXED;
    }
    memcpy(out_partition->rgba[source] + rgba_offset, resolved_rgba + rgba_offset, 4U);
    out_partition->coverage[source][pixel] = 255U;
  }
  return 0;
}
