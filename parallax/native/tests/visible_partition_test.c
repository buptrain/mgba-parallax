#include "gba_xr_visible_partition.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; } } while (0)

int main(void) {
  static uint8_t resolved[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4U];
  static gbaxr_pixel_provenance provenance[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];
  /* The six full-resolution color planes and masks exceed the default
   * Windows test-process stack. Keep this fixture static like its input. */
  static gbaxr_visible_partition partition;

  memset(resolved, 0, sizeof(resolved));
  memset(provenance, 0, sizeof(provenance));
  resolved[0] = 1U; resolved[1] = 2U; resolved[2] = 3U; resolved[3] = 255U;
  resolved[4] = 4U; resolved[5] = 5U; resolved[6] = 6U; resolved[7] = 255U;
  provenance[0].source = GBAXR_SOURCE_BG0;
  provenance[0].priority = 2U;
  provenance[1].source = GBAXR_SOURCE_OBJ;
  provenance[1].priority = 1U;

  CHECK(gbaxr_build_visible_partition(resolved, provenance, &partition) == 0);
  CHECK(partition.present[GBAXR_SOURCE_BG0] == 1U);
  CHECK(partition.present[GBAXR_SOURCE_OBJ] == 1U);
  CHECK(partition.coverage[GBAXR_SOURCE_BG0][0] == 255U);
  CHECK(partition.coverage[GBAXR_SOURCE_BG0][1] == 0U);
  CHECK(partition.coverage[GBAXR_SOURCE_OBJ][0] == 0U);
  CHECK(partition.coverage[GBAXR_SOURCE_OBJ][1] == 255U);
  CHECK(partition.rgba[GBAXR_SOURCE_BG0][0] == 1U);
  CHECK(partition.rgba[GBAXR_SOURCE_OBJ][4] == 4U);
  CHECK(partition.priority[GBAXR_SOURCE_BG0] == 2U);
  CHECK(partition.priority[GBAXR_SOURCE_OBJ] == 1U);

  provenance[2].source = 99U;
  CHECK(gbaxr_build_visible_partition(resolved, provenance, &partition) == -1);
  CHECK(partition.present[GBAXR_SOURCE_BG0] == 0U);
  puts("visible partition tests passed");
  return 0;
}
