#ifndef EMUXR_MGBA_PARITY_H
#define EMUXR_MGBA_PARITY_H

#include <stdint.h>

#include "gba_xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  EMUXR_MGBA_PARITY_MAX_FRAMES = 16U,
  EMUXR_MGBA_PARITY_CANONICAL_PRETRACE_MASK_COUNT = 2U
};

typedef enum emuxr_mgba_parity_reason {
  EMUXR_MGBA_PARITY_PASS = 0,
  EMUXR_MGBA_PARITY_INVALID_ARGUMENT,
  EMUXR_MGBA_PARITY_INIT_FAILED,
  EMUXR_MGBA_PARITY_INIT_TIMEOUT,
  EMUXR_MGBA_PARITY_FRAME_TIMEOUT,
  EMUXR_MGBA_PARITY_SEQUENCE_MISMATCH,
  EMUXR_MGBA_PARITY_FRAME_HASH_MISMATCH,
  EMUXR_MGBA_PARITY_AUDIO_RATE_MISMATCH,
  EMUXR_MGBA_PARITY_INPUT_DISPATCH_MISMATCH,
  EMUXR_MGBA_PARITY_STATE_REJECT_MISMATCH,
  EMUXR_MGBA_PARITY_STATE_SAVE_MISMATCH,
  EMUXR_MGBA_PARITY_STATE_LOAD_MISMATCH,
  /* A backend did not reproduce its own post-checkpoint frame after load. */
  EMUXR_MGBA_PARITY_STATE_REPLAY_MISMATCH
} emuxr_mgba_parity_reason;

typedef struct emuxr_mgba_parity_observation {
  uint32_t frame_count;
  uint64_t frame_sequences[EMUXR_MGBA_PARITY_MAX_FRAMES];
  uint64_t frame_hashes[EMUXR_MGBA_PARITY_MAX_FRAMES];
  /* First copied RGBA pixel for each observed frame; diagnostic only. */
  uint32_t frame_first_rgba[EMUXR_MGBA_PARITY_MAX_FRAMES];
  uint32_t output_audio_sample_rate;
  uint32_t audio_available_frames;
  uint32_t audio_read_frames;
  uint64_t audio_read_hash;
  uint32_t dispatched_joypad_mask;
  int invalid_state_status;
  int save_state_status;
  int load_state_status;
  /* Diagnostic only: audio is not required to be byte-identical. */
  uint64_t state_advanced_frame_hash;
  uint64_t state_replayed_frame_hash;
  uint64_t state_advanced_audio_hash;
  uint64_t state_replayed_audio_hash;
  int state_replay_matches;
} emuxr_mgba_parity_observation;

typedef struct emuxr_mgba_parity_result {
  int passed;
  emuxr_mgba_parity_reason reason;
  emuxr_mgba_parity_observation direct;
  emuxr_mgba_parity_observation libretro;
} emuxr_mgba_parity_result;

/* Pure contract evaluator. Frame/audio/state bytes are not compared beyond
 * copied resolved RGBA hashes and the explicit minimal contract below. */
int emuxr_mgba_parity_evaluate(uint32_t expected_frame_count,
                               const emuxr_mgba_parity_observation *direct,
                               const emuxr_mgba_parity_observation *libretro,
                               emuxr_mgba_parity_result *out_result);
/* Fixture-only, unobserved initial transition: A press then release. */
int emuxr_mgba_parity_canonical_pretrace_mask(uint32_t index,
                                              uint16_t *out_mask);
const char *emuxr_mgba_parity_reason_name(emuxr_mgba_parity_reason reason);

#ifdef __cplusplus
}
#endif

#endif
