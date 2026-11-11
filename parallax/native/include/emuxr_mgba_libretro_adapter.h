#ifndef EMUXR_MGBA_LIBRETRO_ADAPTER_H
#define EMUXR_MGBA_LIBRETRO_ADAPTER_H

/* Experimental wrapper for the pinned mGBA libretro core. This is not the
 * direct-mGBA Unity plug-in ABI. The header intentionally exposes neither
 * libretro nor mGBA types. */

#include "gba_xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emuxr_mgba_libretro_adapter emuxr_mgba_libretro_adapter;

enum {
  EMUXR_MGBA_LIBRETRO_ADAPTER_STARTING = 0,
  EMUXR_MGBA_LIBRETRO_ADAPTER_RUNNING = 1,
  EMUXR_MGBA_LIBRETRO_ADAPTER_FAILED = -1
};

/* A stopped adapter is intentionally not restartable: the resolved-frame
 * bridge has one producer-thread binding. Destroy and recreate it for a new
 * ROM session. */
GBAXR_API emuxr_mgba_libretro_adapter *emuxr_mgba_libretro_adapter_create(
    uint32_t requested_bridge_abi_version);
GBAXR_API int emuxr_mgba_libretro_adapter_start(
    emuxr_mgba_libretro_adapter *adapter,
    const char *utf8_rom_path,
    uint32_t frames_per_second);
GBAXR_API int emuxr_mgba_libretro_adapter_state(
    const emuxr_mgba_libretro_adapter *adapter);

/* Frame-consumer facade for a future Unity binding. The bridge context is
 * intentionally adapter-private: it was allocated into this shared object's
 * bridge implementation and must never be passed to gbaxr_* symbols resolved
 * from direct libgba_xr.so. Lease data stays valid until the paired release.
 * The normal bridge status values are returned, including GBAXR_NO_FRAME and
 * GBAXR_WRONG_THREAD. These calls are safe before start; acquire then returns
 * GBAXR_NO_FRAME until the worker publishes. */
GBAXR_API gbaxr_status emuxr_mgba_libretro_adapter_acquire_latest_frame(
    emuxr_mgba_libretro_adapter *adapter,
    gbaxr_frame_lease *out_lease);
GBAXR_API gbaxr_status emuxr_mgba_libretro_adapter_release_frame(
    emuxr_mgba_libretro_adapter *adapter,
    const gbaxr_frame_lease *lease);

GBAXR_API void emuxr_mgba_libretro_adapter_set_joypad_mask(
    emuxr_mgba_libretro_adapter *adapter,
    uint16_t mask);
GBAXR_API uint32_t emuxr_mgba_libretro_adapter_read_audio_pcm_s16(
    emuxr_mgba_libretro_adapter *adapter,
    int16_t *stereo_interleaved,
    uint32_t max_frames);
GBAXR_API uint32_t emuxr_mgba_libretro_adapter_audio_available_frames(
    const emuxr_mgba_libretro_adapter *adapter);
GBAXR_API uint32_t emuxr_mgba_libretro_adapter_audio_sample_rate(
    const emuxr_mgba_libretro_adapter *adapter);
GBAXR_API uint32_t emuxr_mgba_libretro_adapter_source_audio_sample_rate(
    const emuxr_mgba_libretro_adapter *adapter);
GBAXR_API uint64_t emuxr_mgba_libretro_adapter_audio_dropped_frames(
    const emuxr_mgba_libretro_adapter *adapter);

GBAXR_API int emuxr_mgba_libretro_adapter_save_state(
    emuxr_mgba_libretro_adapter *adapter,
    const char *utf8_state_path);
GBAXR_API int emuxr_mgba_libretro_adapter_load_state(
    emuxr_mgba_libretro_adapter *adapter,
    const char *utf8_state_path);
GBAXR_API void emuxr_mgba_libretro_adapter_stop(
    emuxr_mgba_libretro_adapter *adapter);
GBAXR_API void emuxr_mgba_libretro_adapter_destroy(
    emuxr_mgba_libretro_adapter *adapter);

#ifdef __cplusplus
}
#endif

#endif
