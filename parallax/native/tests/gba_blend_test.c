#include "gba_xr_gba_blend.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)

int main(void) {
  const uint8_t magenta[4] = {255U, 0U, 255U, 255U};
  const uint8_t cyan[4] = {0U, 255U, 255U, 255U};
  uint8_t result[4];
  gbaxr_gba_alpha_rgba(magenta, cyan, 8U, 8U, result);
  CHECK(result[0] == 127U && result[1] == 127U && result[2] == 255U && result[3] == 255U);
  CHECK(gbaxr_gba_alpha_channel(255U, 255U, 16U, 16U) == 255U);
  CHECK(gbaxr_gba_alpha_channel(255U, 0U, 17U, 0U) == 255U);
  puts("gba blend tests passed");
  return 0;
}
