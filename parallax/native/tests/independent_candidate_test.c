#include "gba_xr_independent_candidate.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; } } while (0)

int main(void) {
  static uint8_t resolved[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4U];
  static uint8_t candidate[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4U];
  static uint8_t coverage[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];
  static uint8_t bg1[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4U];
  static uint8_t bg1_coverage[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];
  static gbaxr_pixel_provenance provenance[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];
  uint32_t priority = 0U;

  memset(resolved, 0x5AU, sizeof(resolved));
  memcpy(candidate, resolved, sizeof(candidate));
  memset(coverage, 255, sizeof(coverage));
  memset(provenance, 0, sizeof(provenance));
  for (uint32_t pixel = 0U; pixel < GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT; ++pixel) {
    provenance[pixel].source = GBAXR_SOURCE_BG0;
    provenance[pixel].priority = 2U;
  }
  CHECK(gbaxr_bg0_candidate_recomposes_resolved(resolved, provenance, candidate, coverage, &priority) == 1);
  CHECK(priority == 2U);

  coverage[17] = 0U;
  CHECK(gbaxr_bg0_candidate_recomposes_resolved(resolved, provenance, candidate, coverage, &priority) == 0);
  coverage[17] = 255U;
  provenance[17].blend_effect = GBAXR_BLEND_ALPHA;
  CHECK(gbaxr_bg0_candidate_recomposes_resolved(resolved, provenance, candidate, coverage, &priority) == 0);
  provenance[17].blend_effect = GBAXR_BLEND_NONE;
  provenance[17].window = 1U;
  CHECK(gbaxr_bg0_candidate_recomposes_resolved(resolved, provenance, candidate, coverage, &priority) == 0);
  provenance[17].window = 0U;
  candidate[17U * 4U] ^= 1U;
  CHECK(gbaxr_bg0_candidate_recomposes_resolved(resolved, provenance, candidate, coverage, &priority) == 0);
  candidate[17U * 4U] ^= 1U;
  provenance[17].source = GBAXR_SOURCE_OBJ;
  CHECK(gbaxr_bg0_candidate_recomposes_resolved(resolved, provenance, candidate, coverage, &priority) == 0);

  memset(coverage, 0, sizeof(coverage));
  memset(bg1_coverage, 255, sizeof(bg1_coverage));
  for (uint32_t pixel = 0U; pixel < GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT; ++pixel) {
    uint8_t *out = resolved + pixel * 4U;
    uint8_t *bg0 = candidate + pixel * 4U;
    uint8_t *bg1_pixel = bg1 + pixel * 4U;
    provenance[pixel].source = pixel % GBAXR_GBA_WIDTH < 120U ? GBAXR_SOURCE_BG0 : GBAXR_SOURCE_BG1;
    provenance[pixel].priority = 2U; provenance[pixel].blend_effect = 0U; provenance[pixel].window = 0U;
    bg0[0] = 0U; bg0[1] = 255U; bg0[2] = 255U; bg0[3] = 255U;
    bg1_pixel[0] = 255U; bg1_pixel[1] = 0U; bg1_pixel[2] = 255U; bg1_pixel[3] = 255U;
    if (provenance[pixel].source == GBAXR_SOURCE_BG0) { coverage[pixel] = 255U; memcpy(out, bg0, 4U); }
    else memcpy(out, bg1_pixel, 4U);
  }
  CHECK(gbaxr_bg0_bg1_candidates_recompose_resolved(resolved, provenance, candidate, coverage, bg1, bg1_coverage, &priority) == 1);
  CHECK(priority == 2U);
  bg1_coverage[0] = 0U;
  CHECK(gbaxr_bg0_bg1_candidates_recompose_resolved(resolved, provenance, candidate, coverage, bg1, bg1_coverage, &priority) == 0);

  puts("independent candidate tests passed");
  return 0;
}
