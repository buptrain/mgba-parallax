#ifndef GBA_XR_INDEPENDENT_CANDIDATE_H
#define GBA_XR_INDEPENDENT_CANDIDATE_H

#include "gba_xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Narrow acceptance gate for the first raw PPU candidate. It returns nonzero
 * only when the captured BG0 pixels are a complete, byte-exact, effect-free
 * explanation of the resolved GBA frame. Callers must fall back otherwise. */
int gbaxr_bg0_candidate_recomposes_resolved(const uint8_t *resolved_rgba,
                                            const gbaxr_pixel_provenance *provenance,
                                            const uint8_t *candidate_rgba,
                                            const uint8_t *candidate_coverage,
                                            uint32_t *out_priority);

/* Strict BG0/BG1 equal-priority candidate gate. BG0 wins a tie, matching the
 * Mode 0 fixture contract. Both candidate masks are raw, so BG1 may cover
 * pixels hidden by BG0 in the resolved frame. */
int gbaxr_bg0_bg1_candidates_recompose_resolved(const uint8_t *resolved_rgba,
                                                const gbaxr_pixel_provenance *provenance,
                                                const uint8_t *bg0_rgba, const uint8_t *bg0_coverage,
                                                const uint8_t *bg1_rgba, const uint8_t *bg1_coverage,
                                                uint32_t *out_priority);
int gbaxr_bg0_obj_candidates_recompose_resolved(const uint8_t *resolved_rgba,
                                                const gbaxr_pixel_provenance *provenance,
                                                const uint8_t *bg0_rgba, const uint8_t *bg0_coverage,
                                                const uint8_t *obj_rgba, const uint8_t *obj_coverage,
                                                uint32_t *out_priority);

#ifdef __cplusplus
}
#endif

#endif
