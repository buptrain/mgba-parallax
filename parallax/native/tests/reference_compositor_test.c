#include "gba_xr_reference_compositor.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; } } while (0)

static gbaxr_composite_layer make_layer(const uint8_t *pixels, uint32_t priority, uint32_t order) {
  gbaxr_composite_layer layer;
  layer.image.rgba8 = pixels;
  layer.image.width = 2U;
  layer.image.height = 1U;
  layer.image.row_stride_bytes = 8U;
  layer.priority = priority;
  layer.stable_order = order;
  layer.blend_mode = GBAXR_REFERENCE_BLEND_OPAQUE;
  return layer;
}

int main(void) {
  const uint8_t backdrop[] = { 10, 20, 30, 255, 10, 20, 30, 255 };
  const uint8_t bg[] =       { 99, 88, 77, 255,  0,  0,  0,   0 };
  const uint8_t obj[] =      {  1,  2,  3, 255,  4,  5,  6, 255 };
  const uint8_t alpha_obj[] ={210, 20, 30, 128,  0,  0,  0,   0 };
  const uint8_t expected[] = {  1,  2,  3, 255,  4,  5,  6, 255 };
  uint8_t actual[8] = {0};
  gbaxr_composite_layer layers[] = {
    make_layer(backdrop, 3U, 0U), make_layer(bg, 2U, 0U), make_layer(obj, 0U, 0U)
  };

  CHECK(gbaxr_reference_composite(layers, 3U, 2U, 1U, actual) == 0);
  CHECK(memcmp(actual, expected, sizeof(expected)) == 0);

  layers[2].priority = 2U;
  layers[2].stable_order = 1U;
  CHECK(gbaxr_reference_composite(layers, 3U, 2U, 1U, actual) == 0);
  CHECK(actual[0] == 99U && actual[1] == 88U && actual[2] == 77U);
  CHECK(actual[4] == 4U && actual[5] == 5U && actual[6] == 6U);

  layers[2] = make_layer(alpha_obj, 0U, 0U);
  layers[2].blend_mode = GBAXR_REFERENCE_BLEND_ALPHA;
  CHECK(gbaxr_reference_composite(layers, 3U, 2U, 1U, actual) == 0);
  CHECK(actual[0] == 155U && actual[1] == 54U && actual[2] == 53U && actual[3] == 255U);

  CHECK(gbaxr_reference_composite(layers, 3U, 0U, 1U, actual) == -1);
  puts("reference compositor tests passed");
  return 0;
}
