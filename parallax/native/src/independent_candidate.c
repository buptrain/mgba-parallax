#include "gba_xr_independent_candidate.h"

#include <string.h>

int gbaxr_bg0_candidate_recomposes_resolved(const uint8_t *resolved_rgba,
                                            const gbaxr_pixel_provenance *provenance,
                                            const uint8_t *candidate_rgba,
                                            const uint8_t *candidate_coverage,
                                            uint32_t *out_priority) {
  uint32_t pixel;
  uint32_t priority;
  if (resolved_rgba == NULL || provenance == NULL || candidate_rgba == NULL ||
      candidate_coverage == NULL || out_priority == NULL) {
    return 0;
  }
  priority = provenance[0].priority;
  for (pixel = 0U; pixel < GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT; ++pixel) {
    if (provenance[pixel].source != GBAXR_SOURCE_BG0 ||
        provenance[pixel].priority != priority ||
        provenance[pixel].blend_effect != GBAXR_BLEND_NONE ||
        provenance[pixel].window != 0U || candidate_coverage[pixel] == 0U ||
        memcmp(candidate_rgba + pixel * 4U, resolved_rgba + pixel * 4U, 4U) != 0) {
      return 0;
    }
  }
  *out_priority = priority;
  return 1;
}

int gbaxr_bg0_bg1_candidates_recompose_resolved(const uint8_t *resolved_rgba,
                                                const gbaxr_pixel_provenance *provenance,
                                                const uint8_t *bg0_rgba, const uint8_t *bg0_coverage,
                                                const uint8_t *bg1_rgba, const uint8_t *bg1_coverage,
                                                uint32_t *out_priority) {
  uint32_t pixel;
  uint32_t priority;
  if (resolved_rgba == NULL || provenance == NULL || bg0_rgba == NULL || bg0_coverage == NULL ||
      bg1_rgba == NULL || bg1_coverage == NULL || out_priority == NULL) return 0;
  priority = provenance[0].priority;
  for (pixel = 0U; pixel < GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT; ++pixel) {
    const uint8_t *selected;
    uint8_t source;
    if (provenance[pixel].source != GBAXR_SOURCE_BG0 && provenance[pixel].source != GBAXR_SOURCE_BG1) return 0;
    if (provenance[pixel].priority != priority || provenance[pixel].blend_effect != GBAXR_BLEND_NONE ||
        provenance[pixel].window != 0U || bg1_coverage[pixel] == 0U) return 0;
    if (bg0_coverage[pixel] != 0U) { selected = bg0_rgba + pixel * 4U; source = GBAXR_SOURCE_BG0; }
    else { selected = bg1_rgba + pixel * 4U; source = GBAXR_SOURCE_BG1; }
    if (provenance[pixel].source != source || memcmp(selected, resolved_rgba + pixel * 4U, 4U) != 0) return 0;
  }
  *out_priority = priority;
  return 1;
}

int gbaxr_bg0_obj_candidates_recompose_resolved(const uint8_t *resolved_rgba,
                                                const gbaxr_pixel_provenance *provenance,
                                                const uint8_t *bg0_rgba, const uint8_t *bg0_coverage,
                                                const uint8_t *obj_rgba, const uint8_t *obj_coverage,
                                                uint32_t *out_priority) {
  uint32_t pixel, priority;
  if (!resolved_rgba || !provenance || !bg0_rgba || !bg0_coverage || !obj_rgba || !obj_coverage || !out_priority) return 0;
  priority = provenance[0].priority;
  for (pixel = 0U; pixel < GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT; ++pixel) {
    const uint8_t *selected; uint8_t source;
    if ((provenance[pixel].source != GBAXR_SOURCE_BG0 && provenance[pixel].source != GBAXR_SOURCE_OBJ) ||
        provenance[pixel].priority != priority || provenance[pixel].blend_effect != GBAXR_BLEND_NONE || provenance[pixel].window != 0U || !bg0_coverage[pixel]) return 0;
    if (obj_coverage[pixel]) { selected = obj_rgba + pixel * 4U; source = GBAXR_SOURCE_OBJ; }
    else { selected = bg0_rgba + pixel * 4U; source = GBAXR_SOURCE_BG0; }
    if (provenance[pixel].source != source || memcmp(selected, resolved_rgba + pixel * 4U, 4U)) return 0;
  }
  *out_priority = priority; return 1;
}
