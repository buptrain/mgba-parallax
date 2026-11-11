#include "emuxr_gba_bridge_sink.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; } } while (0)

int main(void) {
  static uint8_t pixels[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4U];
  gbaxr_context *bridge = gbaxr_create(GBAXR_ABI_VERSION);
  gbaxr_frame_lease lease;
  emuxr_rgba_frame frame = {
    pixels,
    GBAXR_GBA_WIDTH,
    GBAXR_GBA_HEIGHT,
    GBAXR_GBA_WIDTH * 4U
  };

  CHECK(bridge != NULL);
  pixels[0] = 0x12U;
  pixels[1] = 0x34U;
  pixels[2] = 0x56U;
  pixels[3] = 0xFFU;
  CHECK(emuxr_gba_bridge_sink_publish(bridge, &frame) == 0);
  pixels[0] = 0U;
  CHECK(gbaxr_acquire_latest_frame(bridge, &lease) == GBAXR_OK);
  CHECK(lease.resolved_rgba.rgba8[0] == 0x12U);
  CHECK(lease.resolved_rgba.rgba8[1] == 0x34U);
  CHECK(gbaxr_release_frame(bridge, &lease) == GBAXR_OK);

  frame.width = GBAXR_GBA_WIDTH - 1U;
  CHECK(emuxr_gba_bridge_sink_publish(bridge, &frame) == -1);
  frame.width = GBAXR_GBA_WIDTH;
  frame.row_stride_bytes = GBAXR_GBA_WIDTH * 4U - 1U;
  CHECK(emuxr_gba_bridge_sink_publish(bridge, &frame) == -1);
  gbaxr_destroy(bridge);
  puts("GBA bridge sink tests passed");
  return 0;
}
