#include "emuxr_mgba_parity.h"

#include "emuxr_mgba_libretro_adapter.h"
#include "gba_xr_mgba_adapter.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Android-native/build-only runner. The caller supplies a synthetic or
 * homebrew ROM path; this program never stages content. */
enum {
  EMUXR_PARITY_RGBA_BYTES = GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4U,
  EMUXR_PARITY_AUDIO_READ_FRAMES = 1024U,
  EMUXR_PARITY_DEFAULT_DEADLINE_MS = 20000U,
  EMUXR_PARITY_PACING_FPS = 120U
};

typedef struct parity_options {
  const char *fixture_path;
  const char *state_directory;
  uint32_t deadline_ms;
  uint32_t input_count;
  uint16_t input_masks[EMUXR_MGBA_PARITY_MAX_FRAMES];
} parity_options;

typedef struct parity_session {
  gbaxr_context *bridge;
  gbaxr_mgba_worker *direct;
  emuxr_mgba_libretro_adapter *libretro;
  int is_direct;
} parity_session;

static uint64_t monotonic_milliseconds(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void sleep_milliseconds(uint32_t milliseconds) {
  struct timespec delay;
  delay.tv_sec = (time_t)(milliseconds / 1000U);
  delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
  nanosleep(&delay, NULL);
}

static uint64_t hash_bytes(const uint8_t *bytes, size_t size) {
  uint64_t hash = UINT64_C(1469598103934665603);
  size_t index;
  for (index = 0U; index < size; ++index) {
    hash ^= bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static int parse_unsigned(const char *text, uint32_t maximum, uint32_t *out_value) {
  char *end = NULL;
  unsigned long value;
  if (text == NULL || out_value == NULL || text[0] == '\0') return -1;
  value = strtoul(text, &end, 0);
  if (end == text || *end != '\0' || value > maximum) return -1;
  *out_value = (uint32_t)value;
  return 0;
}

/* Script masks use public GBA key order. Translate only at the libretro edge. */
static int parse_input_script(const char *text, parity_options *options) {
  const char *cursor = text;
  char *end = NULL;
  if (text == NULL || options == NULL || text[0] == '\0') return -1;
  while (*cursor != '\0') {
    unsigned long value;
    if (options->input_count >= EMUXR_MGBA_PARITY_MAX_FRAMES) return -1;
    value = strtoul(cursor, &end, 0);
    if (end == cursor || value > UINT16_MAX) return -1;
    options->input_masks[options->input_count++] = (uint16_t)value;
    if (*end == '\0') break;
    if (*end != ',') return -1;
    cursor = end + 1;
    if (*cursor == '\0') return -1;
  }
  return options->input_count > 0U ? 0 : -1;
}

static int parse_options(int argc, char **argv, parity_options *out_options) {
  int index;
  uint32_t parsed;
  uint32_t requested_count = 0U;
  const char *input_script = NULL;
  if (out_options == NULL) return -1;
  memset(out_options, 0, sizeof(*out_options));
  out_options->deadline_ms = EMUXR_PARITY_DEFAULT_DEADLINE_MS;
  for (index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "--fixture") == 0 && index + 1 < argc) {
      out_options->fixture_path = argv[++index];
    } else if (strcmp(argv[index], "--state-dir") == 0 && index + 1 < argc) {
      out_options->state_directory = argv[++index];
    } else if (strcmp(argv[index], "--max-frames") == 0 && index + 1 < argc &&
               parse_unsigned(argv[++index], EMUXR_MGBA_PARITY_MAX_FRAMES, &parsed) == 0 &&
               parsed > 0U) {
      requested_count = parsed;
    } else if (strcmp(argv[index], "--deadline-ms") == 0 && index + 1 < argc &&
               parse_unsigned(argv[++index], 60000U, &parsed) == 0 && parsed > 0U) {
      out_options->deadline_ms = parsed;
    } else if (strcmp(argv[index], "--input-script") == 0 && index + 1 < argc) {
      input_script = argv[++index];
    } else {
      return -1;
    }
  }
  if (out_options->fixture_path == NULL || out_options->state_directory == NULL ||
      input_script == NULL || parse_input_script(input_script, out_options) != 0 ||
      (requested_count != 0U && requested_count != out_options->input_count)) return -1;
  return 0;
}

static int copy_and_hash_lease(const gbaxr_frame_lease *lease, uint8_t *copy,
                               uint64_t *out_hash) {
  uint32_t row;
  if (lease == NULL || copy == NULL || out_hash == NULL ||
      lease->resolved_rgba.rgba8 == NULL ||
      lease->resolved_rgba.width != GBAXR_GBA_WIDTH ||
      lease->resolved_rgba.height != GBAXR_GBA_HEIGHT ||
      lease->resolved_rgba.row_stride_bytes < GBAXR_GBA_WIDTH * 4U) return -1;
  for (row = 0U; row < GBAXR_GBA_HEIGHT; ++row) {
    memcpy(copy + row * GBAXR_GBA_WIDTH * 4U,
           lease->resolved_rgba.rgba8 + row * lease->resolved_rgba.row_stride_bytes,
           GBAXR_GBA_WIDTH * 4U);
  }
  *out_hash = hash_bytes(copy, EMUXR_PARITY_RGBA_BYTES);
  return 0;
}

static int session_is_running(const parity_session *session) {
  return session->is_direct
      ? gbaxr_mgba_worker_state(session->direct) == GBAXR_MGBA_WORKER_RUNNING
      : emuxr_mgba_libretro_adapter_state(session->libretro) ==
          EMUXR_MGBA_LIBRETRO_ADAPTER_RUNNING;
}

static int session_has_failed(const parity_session *session) {
  return session->is_direct
      ? gbaxr_mgba_worker_state(session->direct) < GBAXR_MGBA_WORKER_STARTING
      : emuxr_mgba_libretro_adapter_state(session->libretro) <
          EMUXR_MGBA_LIBRETRO_ADAPTER_STARTING;
}

static uint16_t gba_to_libretro_joypad_mask(uint16_t gba_mask) {
  /* GBA: A,B,Select,Start,Right,Left,Up,Down,R,L. Libretro: B,Y,Select,
   * Start,Up,Down,Left,Right,A,X,L,R. */
  static const uint8_t button_for_gba[10] = {8U, 0U, 2U, 3U, 7U,
                                             6U, 4U, 5U, 11U, 10U};
  uint16_t libretro_mask = 0U;
  uint32_t bit;
  for (bit = 0U; bit < 10U; ++bit) {
    if ((gba_mask & (uint16_t)(1U << bit)) != 0U)
      libretro_mask |= (uint16_t)(1U << button_for_gba[bit]);
  }
  return libretro_mask;
}

static void session_set_joypad(parity_session *session, uint16_t gba_mask) {
  if (session->is_direct) gbaxr_mgba_worker_set_keys(session->direct, gba_mask);
  else emuxr_mgba_libretro_adapter_set_joypad_mask(session->libretro,
                                                    gba_to_libretro_joypad_mask(gba_mask));
}

static uint32_t session_audio_rate(const parity_session *session) {
  return session->is_direct ? gbaxr_mgba_worker_audio_pcm_sample_rate(session->direct)
      : emuxr_mgba_libretro_adapter_audio_sample_rate(session->libretro);
}

static uint32_t session_audio_available(const parity_session *session) {
  return session->is_direct ? gbaxr_mgba_worker_audio_available_frames(session->direct)
      : emuxr_mgba_libretro_adapter_audio_available_frames(session->libretro);
}

static uint32_t session_audio_read(parity_session *session, int16_t *pcm, uint32_t frames) {
  return session->is_direct ? gbaxr_mgba_worker_read_audio_pcm_s16(session->direct, pcm, frames)
      : emuxr_mgba_libretro_adapter_read_audio_pcm_s16(session->libretro, pcm, frames);
}

static int session_save(parity_session *session, const char *path) {
  return session->is_direct ? gbaxr_mgba_worker_save_state(session->direct, path)
      : emuxr_mgba_libretro_adapter_save_state(session->libretro, path);
}

static int session_load(parity_session *session, const char *path) {
  return session->is_direct ? gbaxr_mgba_worker_load_state(session->direct, path)
      : emuxr_mgba_libretro_adapter_load_state(session->libretro, path);
}

static gbaxr_status session_acquire_latest_frame(parity_session *session,
                                                 gbaxr_frame_lease *out_lease) {
  return session->is_direct
      ? gbaxr_acquire_latest_frame(session->bridge, out_lease)
      : emuxr_mgba_libretro_adapter_acquire_latest_frame(session->libretro,
                                                          out_lease);
}

static gbaxr_status session_release_frame(parity_session *session,
                                          const gbaxr_frame_lease *lease) {
  return session->is_direct
      ? gbaxr_release_frame(session->bridge, lease)
      : emuxr_mgba_libretro_adapter_release_frame(session->libretro, lease);
}

static void session_destroy(parity_session *session) {
  if (session->is_direct) {
    if (session->direct != NULL) gbaxr_mgba_worker_destroy(session->direct);
    if (session->bridge != NULL) gbaxr_destroy(session->bridge);
  } else if (session->libretro != NULL) {
    emuxr_mgba_libretro_adapter_destroy(session->libretro);
  }
  memset(session, 0, sizeof(*session));
}

static int session_start(parity_session *session, int is_direct, const char *fixture_path) {
  memset(session, 0, sizeof(*session));
  session->is_direct = is_direct;
  if (is_direct) {
    session->bridge = gbaxr_create_unbound(GBAXR_ABI_VERSION);
    if (session->bridge == NULL) return -1;
    session->direct = gbaxr_mgba_worker_start(session->bridge, fixture_path,
                                               EMUXR_PARITY_PACING_FPS);
    return session->direct == NULL ? -1 : 0;
  }
  session->libretro = emuxr_mgba_libretro_adapter_create(GBAXR_ABI_VERSION);
  if (session->libretro == NULL ||
      emuxr_mgba_libretro_adapter_start(session->libretro, fixture_path,
                                        EMUXR_PARITY_PACING_FPS) != GBAXR_OK) return -1;
  return 0;
}

static int make_state_path(char *destination, size_t destination_size,
                           const char *directory, const char *name) {
  const size_t length = strlen(directory);
  const int separator = length > 0U && directory[length - 1U] != '/';
  const int written = snprintf(destination, destination_size, "%s%s%s", directory,
                               separator ? "/" : "", name);
  return written >= 0 && (size_t)written < destination_size ? 0 : -1;
}

/* Releases every observed lease on the sole consumer thread. The deadline is
 * one absolute watchdog for both fresh backends, not a per-session timeout. */
static int capture_next_frame(parity_session *session, uint64_t min_sequence,
                              uint64_t deadline, uint8_t *copy,
                              uint64_t *out_sequence, uint64_t *out_hash) {
  while (monotonic_milliseconds() < deadline) {
    gbaxr_frame_lease lease;
    const gbaxr_status status = session_acquire_latest_frame(session, &lease);
    if (status == GBAXR_NO_FRAME) {
      sleep_milliseconds(1U);
      continue;
    }
    if (status != GBAXR_OK) return -1;
    if (lease.sequence <= min_sequence) {
      if (session_release_frame(session, &lease) != GBAXR_OK) return -1;
      sleep_milliseconds(1U);
      continue;
    }
    if (copy_and_hash_lease(&lease, copy, out_hash) != 0 ||
        session_release_frame(session, &lease) != GBAXR_OK) return -1;
    *out_sequence = lease.sequence;
    return 0;
  }
  return -1;
}

/* Discard two released-key frames before comparing the script. At startup the
 * worker may publish before the fixture's first VBlank draw; after a load it
 * may have published the pre-load frame before servicing the state command.
 * Keeping both transport-boundary frames out makes comparisons solely the
 * fixture's one-VBlank input protocol. */
static int settle_fixture(parity_session *session, uint64_t deadline,
                          uint64_t *io_sequence, uint8_t *copy) {
  uint64_t ignored_hash = 0U;
  uint32_t frame;
  session_set_joypad(session, 0U);
  for (frame = 0U; frame < 2U; ++frame) {
    if (capture_next_frame(session, *io_sequence, deadline, copy,
                           io_sequence, &ignored_hash) != 0) return -1;
  }
  return 0;
}

/* Hold one logical input value through two emulated frames. Only the second
 * frame is stable enough to observe or compare; the first establishes the
 * producer-thread input boundary. */
static int hold_input_mask(parity_session *session, uint16_t mask,
                           uint64_t deadline, uint64_t *io_sequence,
                           uint8_t *copy, uint64_t *out_stable_hash) {
  uint64_t ignored_hash = 0U;
  session_set_joypad(session, mask);
  if (capture_next_frame(session, *io_sequence, deadline, copy,
                         io_sequence, &ignored_hash) != 0 ||
      capture_next_frame(session, *io_sequence, deadline, copy,
                         io_sequence, out_stable_hash) != 0) return -1;
  return 0;
}

/* Fresh backends can expose a different first released-input visual despite
 * equivalent steady-state execution. Establish the same fixture state with an
 * unobserved A rising edge, then release it before the compared script. This
 * intentionally runs only before the initial trace: doing it after load would
 * mutate the restored checkpoint and invalidate behavioral replay. */
static int canonicalize_pre_trace_input(parity_session *session,
                                        uint64_t deadline,
                                        uint64_t *io_sequence,
                                        uint8_t *copy) {
  uint64_t ignored_hash = 0U;
  uint16_t mask;
  uint32_t index;
  for (index = 0U; index < EMUXR_MGBA_PARITY_CANONICAL_PRETRACE_MASK_COUNT; ++index) {
    if (emuxr_mgba_parity_canonical_pretrace_mask(index, &mask) != 0 ||
        hold_input_mask(session, mask, deadline, io_sequence, copy, &ignored_hash) != 0) {
      return -1;
    }
  }
  return 0;
}

static int run_script(parity_session *session, const parity_options *options,
                      uint64_t deadline, uint64_t *io_sequence,
                      uint64_t *out_final_hash, uint8_t *copy,
                      emuxr_mgba_parity_observation *observation) {
  uint32_t index;
  uint64_t sequence = *io_sequence;
  for (index = 0U; index < options->input_count; ++index) {
    uint64_t hash = 0U;
    if (hold_input_mask(session, options->input_masks[index], deadline,
                        &sequence, copy, &hash) != 0) return -1;
    if (observation != NULL) {
      observation->frame_sequences[index] = sequence;
      observation->frame_hashes[index] = hash;
      observation->frame_first_rgba[index] =
          ((uint32_t)copy[0] << 24U) | ((uint32_t)copy[1] << 16U) |
          ((uint32_t)copy[2] << 8U) | (uint32_t)copy[3];
      observation->frame_count = index + 1U;
    }
    *out_final_hash = hash;
  }
  *io_sequence = sequence;
  return 0;
}

static uint64_t capture_audio_hash(parity_session *session,
                                   emuxr_mgba_parity_observation *observation,
                                   int16_t *pcm) {
  uint32_t read_frames;
  observation->audio_available_frames = session_audio_available(session);
  read_frames = session_audio_read(session, pcm, EMUXR_PARITY_AUDIO_READ_FRAMES);
  observation->audio_read_frames = read_frames;
  observation->audio_read_hash = hash_bytes((const uint8_t *)pcm,
      (size_t)read_frames * sizeof(int16_t) * 2U);
  return observation->audio_read_hash;
}

static emuxr_mgba_parity_reason run_session(parity_session *session,
                                            const parity_options *options,
                                            const char *state_name,
                                            uint64_t deadline,
                                            emuxr_mgba_parity_observation *observation) {
  char state_path[1024];
  int16_t pcm[EMUXR_PARITY_AUDIO_READ_FRAMES * 2U];
  uint8_t copied[EMUXR_PARITY_RGBA_BYTES];
  uint64_t sequence = 0U;
  uint64_t ignored_hash = 0U;
  memset(observation, 0, sizeof(*observation));
  while (monotonic_milliseconds() < deadline && !session_is_running(session) &&
         !session_has_failed(session)) sleep_milliseconds(1U);
  if (!session_is_running(session)) return session_has_failed(session)
      ? EMUXR_MGBA_PARITY_INIT_FAILED : EMUXR_MGBA_PARITY_INIT_TIMEOUT;
  if (settle_fixture(session, deadline, &sequence, copied) != 0)
    return EMUXR_MGBA_PARITY_FRAME_TIMEOUT;
  if (canonicalize_pre_trace_input(session, deadline, &sequence, copied) != 0)
    return EMUXR_MGBA_PARITY_FRAME_TIMEOUT;
  if (run_script(session, options, deadline, &sequence, &ignored_hash, copied,
                 observation) != 0) return EMUXR_MGBA_PARITY_FRAME_TIMEOUT;
  observation->dispatched_joypad_mask = options->input_masks[options->input_count - 1U];
  observation->output_audio_sample_rate = session_audio_rate(session);
  (void)capture_audio_hash(session, observation, pcm);
  observation->invalid_state_status = session_save(session, "");
  if (make_state_path(state_path, sizeof(state_path), options->state_directory,
                      state_name) != 0) return EMUXR_MGBA_PARITY_INVALID_ARGUMENT;
  remove(state_path);
  observation->save_state_status = session_save(session, state_path);
  if (observation->save_state_status != GBAXR_OK) return EMUXR_MGBA_PARITY_PASS;
  if (run_script(session, options, deadline, &sequence,
                 &observation->state_advanced_frame_hash, copied, NULL) != 0)
    return EMUXR_MGBA_PARITY_FRAME_TIMEOUT;
  observation->state_advanced_audio_hash = capture_audio_hash(session, observation, pcm);
  observation->load_state_status = session_load(session, state_path);
  remove(state_path);
  if (observation->load_state_status != GBAXR_OK) return EMUXR_MGBA_PARITY_PASS;
  if (settle_fixture(session, deadline, &sequence, copied) != 0)
    return EMUXR_MGBA_PARITY_FRAME_TIMEOUT;
  if (run_script(session, options, deadline, &sequence,
                 &observation->state_replayed_frame_hash, copied, NULL) != 0)
    return EMUXR_MGBA_PARITY_FRAME_TIMEOUT;
  observation->state_replayed_audio_hash = capture_audio_hash(session, observation, pcm);
  observation->state_replay_matches =
      observation->state_advanced_frame_hash == observation->state_replayed_frame_hash;
  return EMUXR_MGBA_PARITY_PASS;
}

static void print_observation_json(const emuxr_mgba_parity_observation *observation) {
  uint32_t index;
  printf("{\"frames\":%u,\"sequences\":[", observation->frame_count);
  for (index = 0U; index < observation->frame_count; ++index)
    printf("%s%" PRIu64, index == 0U ? "" : ",", observation->frame_sequences[index]);
  printf("],\"hashes\":[");
  for (index = 0U; index < observation->frame_count; ++index)
    printf("%s\"%016" PRIX64 "\"", index == 0U ? "" : ",", observation->frame_hashes[index]);
  printf("],\"firstRgba\":[");
  for (index = 0U; index < observation->frame_count; ++index)
    printf("%s\"%08" PRIX32 "\"", index == 0U ? "" : ",",
           observation->frame_first_rgba[index]);
  printf("],\"audioRate\":%u,\"audioAvailable\":%u,\"audioRead\":%u,"
         "\"audioHash\":\"%016" PRIX64 "\",\"joypadMask\":%u,\"state\":[%d,%d,%d],"
         "\"stateAdvanceHash\":\"%016" PRIX64 "\",\"stateReplayHash\":\"%016" PRIX64 "\","
         "\"stateAdvanceAudioHash\":\"%016" PRIX64 "\",\"stateReplayAudioHash\":\"%016" PRIX64 "\","
         "\"stateReplayMatch\":%s}", observation->output_audio_sample_rate,
         observation->audio_available_frames, observation->audio_read_frames,
         observation->audio_read_hash, observation->dispatched_joypad_mask,
         observation->invalid_state_status, observation->save_state_status,
         observation->load_state_status, observation->state_advanced_frame_hash,
         observation->state_replayed_frame_hash, observation->state_advanced_audio_hash,
         observation->state_replayed_audio_hash,
         observation->state_replay_matches ? "true" : "false");
}

static void print_result(const emuxr_mgba_parity_result *result) {
  printf("EMUXR_PARITY_V1 {\"pass\":%s,\"reason\":\"%s\",\"direct\":",
         result->passed ? "true" : "false",
         emuxr_mgba_parity_reason_name(result->reason));
  print_observation_json(&result->direct);
  printf(",\"libretro\":");
  print_observation_json(&result->libretro);
  printf("}\n");
}

int main(int argc, char **argv) {
  parity_options options;
  parity_session session;
  emuxr_mgba_parity_observation direct = {0};
  emuxr_mgba_parity_observation libretro = {0};
  emuxr_mgba_parity_result result = {0};
  emuxr_mgba_parity_reason status;
  uint64_t deadline;
  if (parse_options(argc, argv, &options) != 0) {
    result.reason = EMUXR_MGBA_PARITY_INVALID_ARGUMENT;
    print_result(&result);
    return 2;
  }
  deadline = monotonic_milliseconds() + options.deadline_ms;
  if (session_start(&session, 1, options.fixture_path) != 0) {
    result.reason = EMUXR_MGBA_PARITY_INIT_FAILED;
    session_destroy(&session);
    print_result(&result);
    return 1;
  }
  status = run_session(&session, &options, "emuxr-direct-parity.ss0", deadline, &direct);
  session_destroy(&session);
  if (status != EMUXR_MGBA_PARITY_PASS) {
    result.direct = direct;
    result.reason = status;
    print_result(&result);
    return 1;
  }
  if (session_start(&session, 0, options.fixture_path) != 0) {
    result.direct = direct;
    result.reason = EMUXR_MGBA_PARITY_INIT_FAILED;
    session_destroy(&session);
    print_result(&result);
    return 1;
  }
  status = run_session(&session, &options, "emuxr-libretro-parity.ss0", deadline, &libretro);
  session_destroy(&session);
  if (status != EMUXR_MGBA_PARITY_PASS) {
    result.direct = direct;
    result.libretro = libretro;
    result.reason = status;
    print_result(&result);
    return 1;
  }
  (void)emuxr_mgba_parity_evaluate(options.input_count, &direct, &libretro, &result);
  print_result(&result);
  return result.passed ? 0 : 1;
}
