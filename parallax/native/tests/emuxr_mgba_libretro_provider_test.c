#include "emuxr_mgba_libretro_provider.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; } } while (0)

static int reject_video(void *opaque, const emuxr_rgba_frame *frame) {
  (void)opaque;
  (void)frame;
  return -1;
}

int main(void) {
  emuxr_libretro_host_config config;
  emuxr_libretro_host *host;
  const emuxr_libretro_api *api = emuxr_mgba_libretro_api();

  memset(&config, 0, sizeof(config));
  config.video_sink = reject_video;
  CHECK(api != NULL);
  CHECK(api->init != NULL);
  CHECK(api->load_game != NULL);
  host = emuxr_libretro_host_create(api, &config);
  CHECK(host != NULL);
  /* mGBA's pinned libretro frontend requests RGB565 during retro_init(). */
  CHECK(emuxr_libretro_host_pixel_format(host) == EMUXR_RETRO_PIXEL_FORMAT_RGB565);
  emuxr_libretro_host_destroy(host);
  puts("mGBA libretro provider startup test passed");
  return 0;
}
