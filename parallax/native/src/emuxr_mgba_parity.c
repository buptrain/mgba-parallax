#include "emuxr_mgba_parity.h"

#include <string.h>

static int fail(emuxr_mgba_parity_result *result,
                emuxr_mgba_parity_reason reason) {
  result->passed = 0;
  result->reason = reason;
  return -1;
}

int emuxr_mgba_parity_evaluate(uint32_t expected_frame_count,
                               const emuxr_mgba_parity_observation *direct,
                               const emuxr_mgba_parity_observation *libretro,
                               emuxr_mgba_parity_result *out_result) {
  uint32_t index;
  if (direct == NULL || libretro == NULL || out_result == NULL ||
      expected_frame_count == 0U ||
      expected_frame_count > EMUXR_MGBA_PARITY_MAX_FRAMES) {
    return -1;
  }
  memset(out_result, 0, sizeof(*out_result));
  out_result->direct = *direct;
  out_result->libretro = *libretro;
  if (direct->frame_count != expected_frame_count ||
      libretro->frame_count != expected_frame_count) {
    return fail(out_result, EMUXR_MGBA_PARITY_FRAME_TIMEOUT);
  }
  for (index = 0U; index < expected_frame_count; ++index) {
    /* Fresh workers may publish on different scheduling boundaries. Require
     * each trace to advance; the deterministic script's RGBA hashes are the
     * cross-backend behavioral evidence. */
    if (direct->frame_sequences[index] == 0U || libretro->frame_sequences[index] == 0U ||
        (index > 0U && (direct->frame_sequences[index] <= direct->frame_sequences[index - 1U] ||
                         libretro->frame_sequences[index] <= libretro->frame_sequences[index - 1U]))) {
      return fail(out_result, EMUXR_MGBA_PARITY_SEQUENCE_MISMATCH);
    }
    if (direct->frame_hashes[index] != libretro->frame_hashes[index]) {
      return fail(out_result, EMUXR_MGBA_PARITY_FRAME_HASH_MISMATCH);
    }
  }
  if (direct->output_audio_sample_rate != 48000U ||
      libretro->output_audio_sample_rate != 48000U) {
    return fail(out_result, EMUXR_MGBA_PARITY_AUDIO_RATE_MISMATCH);
  }
  if (direct->dispatched_joypad_mask != libretro->dispatched_joypad_mask) {
    return fail(out_result, EMUXR_MGBA_PARITY_INPUT_DISPATCH_MISMATCH);
  }
  if (direct->invalid_state_status != GBAXR_INVALID_ARGUMENT ||
      libretro->invalid_state_status != GBAXR_INVALID_ARGUMENT) {
    return fail(out_result, EMUXR_MGBA_PARITY_STATE_REJECT_MISMATCH);
  }
  if (direct->save_state_status != GBAXR_OK ||
      libretro->save_state_status != GBAXR_OK) {
    return fail(out_result, EMUXR_MGBA_PARITY_STATE_SAVE_MISMATCH);
  }
  if (direct->load_state_status != GBAXR_OK ||
      libretro->load_state_status != GBAXR_OK) {
    return fail(out_result, EMUXR_MGBA_PARITY_STATE_LOAD_MISMATCH);
  }
  if (!direct->state_replay_matches || !libretro->state_replay_matches ||
      direct->state_advanced_frame_hash != direct->state_replayed_frame_hash ||
      libretro->state_advanced_frame_hash != libretro->state_replayed_frame_hash ||
      direct->state_replayed_frame_hash != libretro->state_replayed_frame_hash) {
    return fail(out_result, EMUXR_MGBA_PARITY_STATE_REPLAY_MISMATCH);
  }
  out_result->passed = 1;
  out_result->reason = EMUXR_MGBA_PARITY_PASS;
  return 0;
}

int emuxr_mgba_parity_canonical_pretrace_mask(uint32_t index,
                                              uint16_t *out_mask) {
  static const uint16_t masks[EMUXR_MGBA_PARITY_CANONICAL_PRETRACE_MASK_COUNT] = {
    1U, /* GBA A */
    0U  /* released */
  };
  if (out_mask == NULL || index >= EMUXR_MGBA_PARITY_CANONICAL_PRETRACE_MASK_COUNT) {
    return -1;
  }
  *out_mask = masks[index];
  return 0;
}

const char *emuxr_mgba_parity_reason_name(emuxr_mgba_parity_reason reason) {
  switch (reason) {
    case EMUXR_MGBA_PARITY_PASS: return "pass";
    case EMUXR_MGBA_PARITY_INVALID_ARGUMENT: return "invalid_argument";
    case EMUXR_MGBA_PARITY_INIT_FAILED: return "init_failed";
    case EMUXR_MGBA_PARITY_INIT_TIMEOUT: return "init_timeout";
    case EMUXR_MGBA_PARITY_FRAME_TIMEOUT: return "frame_timeout";
    case EMUXR_MGBA_PARITY_SEQUENCE_MISMATCH: return "sequence_mismatch";
    case EMUXR_MGBA_PARITY_FRAME_HASH_MISMATCH: return "frame_hash_mismatch";
    case EMUXR_MGBA_PARITY_AUDIO_RATE_MISMATCH: return "audio_rate_mismatch";
    case EMUXR_MGBA_PARITY_INPUT_DISPATCH_MISMATCH: return "input_dispatch_mismatch";
    case EMUXR_MGBA_PARITY_STATE_REJECT_MISMATCH: return "state_reject_mismatch";
    case EMUXR_MGBA_PARITY_STATE_SAVE_MISMATCH: return "state_save_mismatch";
    case EMUXR_MGBA_PARITY_STATE_LOAD_MISMATCH: return "state_load_mismatch";
    case EMUXR_MGBA_PARITY_STATE_REPLAY_MISMATCH: return "state_replay_mismatch";
    default: return "unknown";
  }
}
