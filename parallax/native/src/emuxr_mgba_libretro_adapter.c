#include "emuxr_mgba_libretro_adapter.h"

#include "emuxr_gba_libretro_worker.h"
#include "emuxr_mgba_libretro_provider.h"

#include <stdlib.h>

struct emuxr_mgba_libretro_adapter {
  gbaxr_context *bridge;
  emuxr_gba_libretro_worker *worker;
  int started;
};

emuxr_mgba_libretro_adapter *emuxr_mgba_libretro_adapter_create(
    uint32_t requested_bridge_abi_version) {
  emuxr_mgba_libretro_adapter *adapter =
      (emuxr_mgba_libretro_adapter *)calloc(1U, sizeof(*adapter));
  if (adapter == NULL) {
    return NULL;
  }
  adapter->bridge = gbaxr_create_unbound(requested_bridge_abi_version);
  if (adapter->bridge == NULL) {
    free(adapter);
    return NULL;
  }
  return adapter;
}

int emuxr_mgba_libretro_adapter_start(emuxr_mgba_libretro_adapter *adapter,
                                      const char *utf8_rom_path,
                                      uint32_t frames_per_second) {
  if (adapter == NULL || utf8_rom_path == NULL || utf8_rom_path[0] == '\0' ||
      frames_per_second == 0U || frames_per_second > 120U) {
    return GBAXR_INVALID_ARGUMENT;
  }
  if (adapter->started || adapter->worker != NULL) {
    return GBAXR_STATE_ERROR;
  }
  adapter->worker = emuxr_gba_libretro_worker_start(
      adapter->bridge, emuxr_mgba_libretro_api(), utf8_rom_path,
      frames_per_second);
  if (adapter->worker == NULL) {
    return GBAXR_STATE_ERROR;
  }
  adapter->started = 1;
  return GBAXR_OK;
}

int emuxr_mgba_libretro_adapter_state(
    const emuxr_mgba_libretro_adapter *adapter) {
  return adapter == NULL || adapter->worker == NULL
      ? EMUXR_MGBA_LIBRETRO_ADAPTER_FAILED
      : emuxr_gba_libretro_worker_state(adapter->worker);
}

gbaxr_status emuxr_mgba_libretro_adapter_acquire_latest_frame(
    emuxr_mgba_libretro_adapter *adapter,
    gbaxr_frame_lease *out_lease) {
  return adapter == NULL ? GBAXR_INVALID_ARGUMENT :
      gbaxr_acquire_latest_frame(adapter->bridge, out_lease);
}

gbaxr_status emuxr_mgba_libretro_adapter_release_frame(
    emuxr_mgba_libretro_adapter *adapter,
    const gbaxr_frame_lease *lease) {
  return adapter == NULL ? GBAXR_INVALID_ARGUMENT :
      gbaxr_release_frame(adapter->bridge, lease);
}

void emuxr_mgba_libretro_adapter_set_joypad_mask(
    emuxr_mgba_libretro_adapter *adapter,
    uint16_t mask) {
  if (adapter != NULL) {
    emuxr_gba_libretro_worker_set_joypad_mask(adapter->worker, mask);
  }
}

uint32_t emuxr_mgba_libretro_adapter_read_audio_pcm_s16(
    emuxr_mgba_libretro_adapter *adapter,
    int16_t *stereo_interleaved,
    uint32_t max_frames) {
  return adapter == NULL ? 0U : emuxr_gba_libretro_worker_read_audio_pcm_s16(
      adapter->worker, stereo_interleaved, max_frames);
}

uint32_t emuxr_mgba_libretro_adapter_audio_available_frames(
    const emuxr_mgba_libretro_adapter *adapter) {
  return adapter == NULL ? 0U : emuxr_gba_libretro_worker_audio_available_frames(
      adapter->worker);
}

uint32_t emuxr_mgba_libretro_adapter_audio_sample_rate(
    const emuxr_mgba_libretro_adapter *adapter) {
  return adapter == NULL ? 0U : emuxr_gba_libretro_worker_audio_sample_rate(
      adapter->worker);
}

uint32_t emuxr_mgba_libretro_adapter_source_audio_sample_rate(
    const emuxr_mgba_libretro_adapter *adapter) {
  return adapter == NULL ? 0U :
      emuxr_gba_libretro_worker_source_audio_sample_rate(adapter->worker);
}

uint64_t emuxr_mgba_libretro_adapter_audio_dropped_frames(
    const emuxr_mgba_libretro_adapter *adapter) {
  return adapter == NULL ? 0U : emuxr_gba_libretro_worker_audio_dropped_frames(
      adapter->worker);
}

int emuxr_mgba_libretro_adapter_save_state(
    emuxr_mgba_libretro_adapter *adapter,
    const char *utf8_state_path) {
  return adapter == NULL ? GBAXR_INVALID_ARGUMENT :
      emuxr_gba_libretro_worker_save_state(adapter->worker, utf8_state_path);
}

int emuxr_mgba_libretro_adapter_load_state(
    emuxr_mgba_libretro_adapter *adapter,
    const char *utf8_state_path) {
  return adapter == NULL ? GBAXR_INVALID_ARGUMENT :
      emuxr_gba_libretro_worker_load_state(adapter->worker, utf8_state_path);
}

void emuxr_mgba_libretro_adapter_stop(emuxr_mgba_libretro_adapter *adapter) {
  if (adapter != NULL) {
    emuxr_gba_libretro_worker_stop(adapter->worker);
  }
}

void emuxr_mgba_libretro_adapter_destroy(emuxr_mgba_libretro_adapter *adapter) {
  if (adapter != NULL) {
    emuxr_gba_libretro_worker_destroy(adapter->worker);
    gbaxr_destroy(adapter->bridge);
    free(adapter);
  }
}
