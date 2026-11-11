#include "emuxr_mgba_libretro_adapter.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; \
} } while (0)

int main(void) {
  emuxr_mgba_libretro_adapter *adapter;
  gbaxr_frame_lease lease;

  CHECK(emuxr_mgba_libretro_adapter_create(GBAXR_ABI_VERSION + 1U) == NULL);
  CHECK(emuxr_mgba_libretro_adapter_start(NULL, "fixture.gba", 60U) == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_state(NULL) < 0);
  CHECK(emuxr_mgba_libretro_adapter_acquire_latest_frame(NULL, &lease) == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_release_frame(NULL, &lease) == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_audio_available_frames(NULL) == 0U);
  CHECK(emuxr_mgba_libretro_adapter_read_audio_pcm_s16(NULL, NULL, 0U) == 0U);
  CHECK(emuxr_mgba_libretro_adapter_save_state(NULL, "state.ss0") == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_load_state(NULL, "state.ss0") == GBAXR_INVALID_ARGUMENT);
  emuxr_mgba_libretro_adapter_stop(NULL);
  emuxr_mgba_libretro_adapter_destroy(NULL);

  adapter = emuxr_mgba_libretro_adapter_create(GBAXR_ABI_VERSION);
  CHECK(adapter != NULL);
  memset(&lease, 0, sizeof(lease));
  CHECK(emuxr_mgba_libretro_adapter_acquire_latest_frame(adapter, NULL) == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_release_frame(adapter, NULL) == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_acquire_latest_frame(adapter, &lease) == GBAXR_NO_FRAME);
  CHECK(emuxr_mgba_libretro_adapter_release_frame(adapter, &lease) == GBAXR_STATE_ERROR);
  CHECK(emuxr_mgba_libretro_adapter_start(adapter, NULL, 60U) == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_start(adapter, "", 60U) == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_start(adapter, "fixture.gba", 0U) == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_start(adapter, "fixture.gba", 121U) == GBAXR_INVALID_ARGUMENT);
  CHECK(emuxr_mgba_libretro_adapter_audio_sample_rate(adapter) == 0U);
  CHECK(emuxr_mgba_libretro_adapter_source_audio_sample_rate(adapter) == 0U);
  CHECK(emuxr_mgba_libretro_adapter_audio_dropped_frames(adapter) == 0U);
  emuxr_mgba_libretro_adapter_stop(adapter);
  emuxr_mgba_libretro_adapter_stop(adapter);
  emuxr_mgba_libretro_adapter_destroy(adapter);
  puts("mGBA libretro adapter ABI tests passed");
  return 0;
}
