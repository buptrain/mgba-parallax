#include "emuxr_mgba_parity.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; \
} } while (0)

static emuxr_mgba_parity_observation valid_observation(void) {
  emuxr_mgba_parity_observation observation;
  memset(&observation, 0, sizeof(observation));
  observation.frame_count = 2U;
  observation.frame_sequences[0] = 1U;
  observation.frame_sequences[1] = 2U;
  observation.frame_hashes[0] = 0x11U;
  observation.frame_hashes[1] = 0x22U;
  observation.output_audio_sample_rate = 48000U;
  observation.dispatched_joypad_mask = 1U;
  observation.invalid_state_status = GBAXR_INVALID_ARGUMENT;
  observation.save_state_status = GBAXR_OK;
  observation.load_state_status = GBAXR_OK;
  observation.state_advanced_frame_hash = 0x44U;
  observation.state_replayed_frame_hash = 0x44U;
  observation.state_advanced_audio_hash = 0x55U;
  observation.state_replayed_audio_hash = 0x66U;
  observation.state_replay_matches = 1;
  return observation;
}

int main(void) {
  emuxr_mgba_parity_observation direct = valid_observation();
  emuxr_mgba_parity_observation libretro = valid_observation();
  emuxr_mgba_parity_result result;
  uint16_t canonical_mask = 0xFFFFU;

  CHECK(emuxr_mgba_parity_canonical_pretrace_mask(0U, &canonical_mask) == 0);
  CHECK(canonical_mask == 1U);
  CHECK(emuxr_mgba_parity_canonical_pretrace_mask(1U, &canonical_mask) == 0);
  CHECK(canonical_mask == 0U);
  CHECK(emuxr_mgba_parity_canonical_pretrace_mask(
      EMUXR_MGBA_PARITY_CANONICAL_PRETRACE_MASK_COUNT, &canonical_mask) != 0);
  CHECK(emuxr_mgba_parity_canonical_pretrace_mask(0U, NULL) != 0);

  CHECK(emuxr_mgba_parity_evaluate(2U, &direct, &libretro, &result) == 0);
  CHECK(result.passed && result.reason == EMUXR_MGBA_PARITY_PASS);
  libretro.frame_hashes[1] = 0x33U;
  CHECK(emuxr_mgba_parity_evaluate(2U, &direct, &libretro, &result) != 0);
  CHECK(result.reason == EMUXR_MGBA_PARITY_FRAME_HASH_MISMATCH);
  libretro = valid_observation();
  libretro.frame_sequences[1] = 1U;
  CHECK(emuxr_mgba_parity_evaluate(2U, &direct, &libretro, &result) != 0);
  CHECK(result.reason == EMUXR_MGBA_PARITY_SEQUENCE_MISMATCH);
  libretro = valid_observation();
  libretro.output_audio_sample_rate = 32768U;
  CHECK(emuxr_mgba_parity_evaluate(2U, &direct, &libretro, &result) != 0);
  CHECK(result.reason == EMUXR_MGBA_PARITY_AUDIO_RATE_MISMATCH);
  libretro = valid_observation();
  libretro.save_state_status = GBAXR_STATE_ERROR;
  CHECK(emuxr_mgba_parity_evaluate(2U, &direct, &libretro, &result) != 0);
  CHECK(result.reason == EMUXR_MGBA_PARITY_STATE_SAVE_MISMATCH);
  libretro = valid_observation();
  libretro.state_replayed_frame_hash = 0x99U;
  libretro.state_replay_matches = 0;
  CHECK(emuxr_mgba_parity_evaluate(2U, &direct, &libretro, &result) != 0);
  CHECK(result.reason == EMUXR_MGBA_PARITY_STATE_REPLAY_MISMATCH);
  libretro = valid_observation();
  libretro.state_advanced_frame_hash = 0x77U;
  libretro.state_replayed_frame_hash = 0x77U;
  /* Both backends can replay themselves while still disagreeing with each
   * other; keep this cross-backend parity requirement strict. */
  CHECK(emuxr_mgba_parity_evaluate(2U, &direct, &libretro, &result) != 0);
  CHECK(result.reason == EMUXR_MGBA_PARITY_STATE_REPLAY_MISMATCH);
  CHECK(emuxr_mgba_parity_evaluate(0U, &direct, &libretro, &result) != 0);
  CHECK(strcmp(emuxr_mgba_parity_reason_name(EMUXR_MGBA_PARITY_PASS), "pass") == 0);
  puts("mGBA parity contract tests passed");
  return 0;
}
