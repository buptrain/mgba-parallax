#ifndef GBA_XR_MGBA_ADAPTER_H
#define GBA_XR_MGBA_ADAPTER_H

#include "gba_xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gbaxr_mgba_session gbaxr_mgba_session;
typedef struct gbaxr_mgba_worker gbaxr_mgba_worker;

enum {
  GBAXR_MGBA_WORKER_STARTING = 0,
  GBAXR_MGBA_WORKER_RUNNING = 1,
  GBAXR_MGBA_WORKER_INIT_FAILED = -1,
  GBAXR_MGBA_WORKER_ROM_LOAD_FAILED = -2,
  GBAXR_MGBA_WORKER_FRAME_FAILED = -3
};

/* Must be called from the bridge producer thread. This adapter owns one mGBA
 * GBA core instance but never exposes mGBA headers/types to Unity. */
GBAXR_API gbaxr_mgba_session *gbaxr_mgba_create(gbaxr_context *bridge);
GBAXR_API void gbaxr_mgba_destroy(gbaxr_mgba_session *session);

/* Loads a user-selected UTF-8 ROM path. The ROM file remains owned by mGBA
 * until unload/destroy. ROMs and BIOS files must never be committed here. */
GBAXR_API gbaxr_status gbaxr_mgba_load_rom(gbaxr_mgba_session *session,
                                           const char *utf8_rom_path);
GBAXR_API void gbaxr_mgba_unload_rom(gbaxr_mgba_session *session);

/* Runs exactly one emulated frame, converts the resolved mGBA framebuffer to
 * non-premultiplied RGBA8, then publishes an immutable mailbox snapshot. */
GBAXR_API gbaxr_status gbaxr_mgba_run_frame(gbaxr_mgba_session *session);

/* Owns a session on one native producer thread. The bridge must be created
 * with gbaxr_create_unbound so its producer affinity belongs to that thread. */
GBAXR_API gbaxr_mgba_worker *gbaxr_mgba_worker_start(gbaxr_context *bridge,
                                                       const char *utf8_rom_path,
                                                       uint32_t frames_per_second);
GBAXR_API int gbaxr_mgba_worker_state(const gbaxr_mgba_worker *worker);
/* Updates the worker pacing target atomically. The change is observed at the
 * next frame boundary and does not touch mGBA state from the Unity thread. */
GBAXR_API int gbaxr_mgba_worker_set_frames_per_second(gbaxr_mgba_worker *worker,
                                                       uint32_t frames_per_second);
/* Thread-safe input mailbox. Bits use the public GBA hardware key order:
 * A=0, B=1, Select=2, Start=3, Right=4, Left=5, Up=6, Down=7, R=8, L=9. */
GBAXR_API void gbaxr_mgba_worker_set_keys(gbaxr_mgba_worker *worker, uint32_t keys);
/* Bounded, thread-safe PCM handoff for exactly one Unity audio consumer.
 * Frames are interleaved stereo signed-16 PCM resampled to 48 kHz by the
 * producer worker; this API never enters mGBA from Unity's audio thread. */
GBAXR_API uint32_t gbaxr_mgba_worker_read_audio_pcm_s16(gbaxr_mgba_worker *worker,
                                                         int16_t *stereo_interleaved,
                                                         uint32_t max_frames);
GBAXR_API uint32_t gbaxr_mgba_worker_audio_available_frames(const gbaxr_mgba_worker *worker);
GBAXR_API uint32_t gbaxr_mgba_worker_audio_pcm_sample_rate(const gbaxr_mgba_worker *worker);
GBAXR_API uint32_t gbaxr_mgba_worker_mgba_audio_sample_rate(const gbaxr_mgba_worker *worker);
GBAXR_API uint64_t gbaxr_mgba_worker_audio_dropped_frames(const gbaxr_mgba_worker *worker);
/* Requests a single-slot state operation. The caller waits only until the
 * worker has reached the next completed-frame boundary and run mGBA's state
 * serialization on its owning thread. Paths must be app-private UTF-8 paths. */
GBAXR_API int gbaxr_mgba_worker_save_state(gbaxr_mgba_worker *worker,
                                           const char *utf8_state_path);
GBAXR_API int gbaxr_mgba_worker_load_state(gbaxr_mgba_worker *worker,
                                           const char *utf8_state_path);
GBAXR_API void gbaxr_mgba_worker_stop(gbaxr_mgba_worker *worker);
GBAXR_API void gbaxr_mgba_worker_destroy(gbaxr_mgba_worker *worker);

#ifdef __cplusplus
}
#endif

#endif
