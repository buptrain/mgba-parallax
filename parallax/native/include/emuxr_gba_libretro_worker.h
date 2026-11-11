#ifndef EMUXR_GBA_LIBRETRO_WORKER_H
#define EMUXR_GBA_LIBRETRO_WORKER_H

#include "emuxr_libretro_host.h"
#include "gba_xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emuxr_gba_libretro_worker emuxr_gba_libretro_worker;

enum {
  EMUXR_LIBRETRO_WORKER_STARTING = 0,
  EMUXR_LIBRETRO_WORKER_RUNNING = 1,
  EMUXR_LIBRETRO_WORKER_INIT_FAILED = -1,
  EMUXR_LIBRETRO_WORKER_ROM_LOAD_FAILED = -2,
  EMUXR_LIBRETRO_WORKER_UNSUPPORTED_VIDEO = -3,
  EMUXR_LIBRETRO_WORKER_FRAME_FAILED = -4
};

/* Owns a standard libretro core on one native producer thread and publishes
 * only 240 x 160 resolved frames through the existing ABI-v3 GBA bridge.
 * Dynamic geometry belongs to the later generic-frame-lease ABI; unsupported
 * core geometry fails closed instead of being cropped or stretched. */
emuxr_gba_libretro_worker *emuxr_gba_libretro_worker_start(
    gbaxr_context *bridge,
    const emuxr_libretro_api *api,
    const char *utf8_rom_path,
    uint32_t frames_per_second);
int emuxr_gba_libretro_worker_state(const emuxr_gba_libretro_worker *worker);
void emuxr_gba_libretro_worker_set_joypad_mask(emuxr_gba_libretro_worker *worker,
                                               uint16_t mask);
uint32_t emuxr_gba_libretro_worker_read_audio_pcm_s16(
    emuxr_gba_libretro_worker *worker,
    int16_t *stereo_interleaved,
    uint32_t max_frames);
uint32_t emuxr_gba_libretro_worker_audio_available_frames(
    const emuxr_gba_libretro_worker *worker);
uint32_t emuxr_gba_libretro_worker_audio_sample_rate(
    const emuxr_gba_libretro_worker *worker);
/* The output ring is always 48 kHz; this reports the core's original rate for
 * diagnostics and parity telemetry. */
uint32_t emuxr_gba_libretro_worker_source_audio_sample_rate(
    const emuxr_gba_libretro_worker *worker);
uint64_t emuxr_gba_libretro_worker_audio_dropped_frames(
    const emuxr_gba_libretro_worker *worker);
int emuxr_gba_libretro_worker_save_state(emuxr_gba_libretro_worker *worker,
                                         const char *utf8_state_path);
int emuxr_gba_libretro_worker_load_state(emuxr_gba_libretro_worker *worker,
                                         const char *utf8_state_path);
void emuxr_gba_libretro_worker_stop(emuxr_gba_libretro_worker *worker);
void emuxr_gba_libretro_worker_destroy(emuxr_gba_libretro_worker *worker);

#ifdef __cplusplus
}
#endif

#endif
