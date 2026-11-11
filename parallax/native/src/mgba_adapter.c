#include "gba_xr_mgba_adapter.h"
#include "gba_xr_mgba_audio_defaults.h"
#include "gba_xr_state_file_policy.h"
#include "gba_xr_independent_candidate.h"
#include "gba_xr_visible_partition.h"
#include "gba_xr_audio_ring.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/serialize.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/image.h>
#include <mgba-util/vfs.h>

enum {
  GBAXR_FRAME_PIXELS = GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT,
  GBAXR_MGBA_AUDIO_BUFFER_FRAMES = 1024U,
  GBAXR_AUDIO_RESAMPLE_BUFFER_FRAMES = 2048U,
  GBAXR_AUDIO_RING_FRAMES = 8192U,
  GBAXR_AUDIO_SCRATCH_FRAMES = 512U,
  GBAXR_AUDIO_PCM_SAMPLE_RATE = 48000U
};

enum gbaxr_state_command {
  GBAXR_STATE_COMMAND_NONE = 0,
  GBAXR_STATE_COMMAND_SAVE = 1,
  GBAXR_STATE_COMMAND_LOAD = 2,
  GBAXR_STATE_COMMAND_RUNNING = 3
};

struct gbaxr_mgba_audio_endpoint {
  gbaxr_audio_ring *ring;
  atomic_uint mgba_sample_rate;
};

struct gbaxr_mgba_session {
  gbaxr_context *bridge;
  struct mCore *core;
  mColor resolved_native[GBAXR_FRAME_PIXELS];
  uint8_t resolved_rgba[GBAXR_FRAME_PIXELS * 4U];
  gbaxr_pixel_provenance provenance[GBAXR_FRAME_PIXELS];
  mColor bg0_candidate_native[GBAXR_FRAME_PIXELS];
  uint8_t bg0_candidate_rgba[GBAXR_FRAME_PIXELS * 4U];
  uint8_t bg0_candidate_coverage[GBAXR_FRAME_PIXELS];
  mColor bg1_candidate_native[GBAXR_FRAME_PIXELS];
  uint8_t bg1_candidate_rgba[GBAXR_FRAME_PIXELS * 4U];
  uint8_t bg1_candidate_coverage[GBAXR_FRAME_PIXELS];
  mColor obj_candidate_native[GBAXR_FRAME_PIXELS];
  uint8_t obj_candidate_rgba[GBAXR_FRAME_PIXELS * 4U];
  uint8_t obj_candidate_coverage[GBAXR_FRAME_PIXELS];
  gbaxr_visible_partition visible_partition;
  int logged_candidate_coverage;
  int rom_loaded;
  struct gbaxr_mgba_audio_endpoint *audio;
  const atomic_uint *frames_per_second;
  uint32_t last_audio_sample_rate;
  uint32_t configured_audio_frames_per_second;
  int owns_audio_endpoint;
  struct mAVStream audio_stream;
  struct mAudioBuffer resampled_audio;
  struct mAudioResampler audio_resampler;
  int16_t audio_scratch[GBAXR_AUDIO_SCRATCH_FRAMES * 2U];
};

struct gbaxr_mgba_worker {
  pthread_t thread;
  atomic_int stop_requested;
  atomic_int state;
  atomic_uint keys;
  atomic_uint frames_per_second;
  pthread_mutex_t state_mutex;
  pthread_cond_t state_complete;
  enum gbaxr_state_command state_command;
  int state_result;
  char *state_path;
  struct gbaxr_mgba_audio_endpoint audio;
  gbaxr_context *bridge;
  char *rom_path;
  int thread_started;
};

static int save_state_to_path(struct mCore *core, const char *path) {
  char *temporary_path;
  size_t path_length;
  struct VFile *file;
  int success;
  int close_success;
  if (core == NULL || path == NULL || path[0] == '\0') {
    return GBAXR_INVALID_ARGUMENT;
  }
  path_length = strlen(path);
  temporary_path = malloc(path_length + sizeof(".tmp"));
  if (temporary_path == NULL) {
    return GBAXR_STATE_ERROR;
  }
  memcpy(temporary_path, path, path_length);
  memcpy(temporary_path + path_length, ".tmp", sizeof(".tmp"));
  file = VFileOpen(temporary_path, GBAXR_STATE_SAVE_OPEN_FLAGS);
  if (file == NULL) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "GbaXr",
                        "GBA XR state: save failed at temporary-file open");
#endif
    free(temporary_path);
    return GBAXR_STATE_ERROR;
  }
  success = mCoreSaveStateNamed(core, file, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
  close_success = file->close(file);
  if (!success) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "GbaXr",
                        "GBA XR state: save failed during mGBA serialization");
#endif
  } else if (!close_success) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "GbaXr",
                        "GBA XR state: save failed while closing temporary file");
#endif
  } else if (rename(temporary_path, path) == 0) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "GbaXr",
                        "GBA XR state: quick slot save committed");
#endif
    free(temporary_path);
    return GBAXR_OK;
  } else {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "GbaXr",
                        "GBA XR state: save failed at atomic commit");
#endif
  }
  remove(temporary_path);
  free(temporary_path);
  return GBAXR_STATE_ERROR;
}

static int load_state_from_path(struct mCore *core, const char *path) {
  struct VFile *file;
  int success;
  if (core == NULL || path == NULL || path[0] == '\0') {
    return GBAXR_INVALID_ARGUMENT;
  }
  file = VFileOpen(path, O_RDONLY);
  if (file == NULL) {
    return GBAXR_STATE_ERROR;
  }
  success = mCoreLoadStateNamed(core, file, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
  file->close(file);
  return success ? GBAXR_OK : GBAXR_STATE_ERROR;
}

static void process_state_command(gbaxr_mgba_worker *worker, gbaxr_mgba_session *session) {
  enum gbaxr_state_command command;
  char *path;
  int result;
  pthread_mutex_lock(&worker->state_mutex);
  command = worker->state_command;
  if (command == GBAXR_STATE_COMMAND_NONE) {
    pthread_mutex_unlock(&worker->state_mutex);
    return;
  }
  path = worker->state_path;
  worker->state_path = NULL;
  worker->state_command = GBAXR_STATE_COMMAND_RUNNING;
  pthread_mutex_unlock(&worker->state_mutex);

  result = command == GBAXR_STATE_COMMAND_SAVE
      ? save_state_to_path(session->core, path)
      : load_state_from_path(session->core, path);
  free(path);

  pthread_mutex_lock(&worker->state_mutex);
  worker->state_result = result;
  worker->state_command = GBAXR_STATE_COMMAND_NONE;
  pthread_cond_broadcast(&worker->state_complete);
  pthread_mutex_unlock(&worker->state_mutex);
}

static int request_state_command(gbaxr_mgba_worker *worker,
                                 enum gbaxr_state_command command,
                                 const char *path) {
  size_t path_length;
  char *path_copy;
  int result;
  if (worker == NULL || path == NULL || path[0] == '\0') {
    return GBAXR_INVALID_ARGUMENT;
  }
  path_length = strlen(path) + 1U;
  path_copy = malloc(path_length);
  if (path_copy == NULL) {
    return GBAXR_STATE_ERROR;
  }
  memcpy(path_copy, path, path_length);
  pthread_mutex_lock(&worker->state_mutex);
  if (atomic_load(&worker->state) != GBAXR_MGBA_WORKER_RUNNING ||
      worker->state_command != GBAXR_STATE_COMMAND_NONE) {
    pthread_mutex_unlock(&worker->state_mutex);
    free(path_copy);
    return GBAXR_STATE_ERROR;
  }
  worker->state_path = path_copy;
  worker->state_result = GBAXR_STATE_ERROR;
  worker->state_command = command;
  while (worker->state_command != GBAXR_STATE_COMMAND_NONE &&
         !atomic_load(&worker->stop_requested)) {
    pthread_cond_wait(&worker->state_complete, &worker->state_mutex);
  }
  result = worker->state_command == GBAXR_STATE_COMMAND_NONE
      ? worker->state_result : GBAXR_STATE_ERROR;
  pthread_mutex_unlock(&worker->state_mutex);
  return result;
}

static void cancel_pending_state_command(gbaxr_mgba_worker *worker) {
  pthread_mutex_lock(&worker->state_mutex);
  free(worker->state_path);
  worker->state_path = NULL;
  worker->state_result = GBAXR_STATE_ERROR;
  worker->state_command = GBAXR_STATE_COMMAND_NONE;
  pthread_cond_broadcast(&worker->state_complete);
  pthread_mutex_unlock(&worker->state_mutex);
}

static void convert_native_to_rgba8(const mColor *source, uint8_t *destination) {
  uint32_t index;
#ifdef COLOR_16_BIT
  for (index = 0U; index < GBAXR_FRAME_PIXELS; ++index) {
    const uint16_t pixel = source[index];
    destination[index * 4U] = (uint8_t)M_R8(pixel);
    destination[index * 4U + 1U] = (uint8_t)M_G8(pixel);
    destination[index * 4U + 2U] = (uint8_t)M_B8(pixel);
    destination[index * 4U + 3U] = 255U;
  }
#else
  /* mGBA's default mColor is XBGR8: in little-endian memory its bytes are
   * R, G, B, X. The adapter normalizes it to explicit RGBA8. */
  for (index = 0U; index < GBAXR_FRAME_PIXELS; ++index) {
    const uint32_t pixel = source[index];
    destination[index * 4U] = (uint8_t)(pixel & 0xFFU);
    destination[index * 4U + 1U] = (uint8_t)((pixel >> 8U) & 0xFFU);
    destination[index * 4U + 2U] = (uint8_t)((pixel >> 16U) & 0xFFU);
    destination[index * 4U + 3U] = 255U;
  }
#endif
}

static void drain_resampled_audio(gbaxr_mgba_session *session) {
  uint32_t available;
  if (session == NULL || session->audio == NULL || session->audio->ring == NULL) {
    return;
  }
  while ((available = (uint32_t)mAudioBufferAvailable(&session->resampled_audio)) != 0U) {
    uint32_t request = available < GBAXR_AUDIO_SCRATCH_FRAMES ? available : GBAXR_AUDIO_SCRATCH_FRAMES;
    uint32_t read = (uint32_t)mAudioBufferRead(&session->resampled_audio, session->audio_scratch, request);
    if (read == 0U) {
      break;
    }
    (void)gbaxr_audio_ring_write(session->audio->ring, session->audio_scratch, read);
  }
}

static uint32_t session_frames_per_second(const gbaxr_mgba_session *session) {
  uint32_t frames_per_second = session->frames_per_second == NULL
      ? 60U
      : atomic_load_explicit(session->frames_per_second, memory_order_acquire);
  return frames_per_second == 0U ? 60U : frames_per_second;
}

static void configure_audio_resampler(gbaxr_mgba_session *session, unsigned sample_rate) {
  uint32_t frames_per_second;
  double fps_ratio;
  double effective_sample_rate;
  if (session == NULL || session->audio == NULL || sample_rate == 0U) {
    return;
  }
  if (session->audio_resampler.source != NULL) {
    (void)mAudioResamplerProcess(&session->audio_resampler);
    drain_resampled_audio(session);
  }
  atomic_store_explicit(&session->audio->mgba_sample_rate, sample_rate, memory_order_release);
  frames_per_second = session_frames_per_second(session);
  fps_ratio = mCoreCalculateFramerateRatio(session->core, (double)frames_per_second);
  effective_sample_rate = fps_ratio > 0.0 ? sample_rate / fps_ratio : sample_rate;
  mAudioResamplerSetSource(&session->audio_resampler,
                           session->core->getAudioBuffer(session->core),
                           effective_sample_rate,
                           true);
  session->last_audio_sample_rate = sample_rate;
  session->configured_audio_frames_per_second = frames_per_second;
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "GbaXr",
                      "audio source=%u Hz effective=%.2f Hz targetFps=%u",
                      sample_rate, effective_sample_rate, frames_per_second);
#endif
}

static void audio_rate_changed(struct mAVStream *stream, unsigned sample_rate) {
  gbaxr_mgba_session *session = (gbaxr_mgba_session *)((char *)stream - offsetof(gbaxr_mgba_session, audio_stream));
  configure_audio_resampler(session, sample_rate);
}

static void refresh_audio_resampler_for_frame_rate(gbaxr_mgba_session *session) {
  uint32_t frames_per_second;
  if (session == NULL || session->last_audio_sample_rate == 0U) {
    return;
  }
  frames_per_second = session_frames_per_second(session);
  if (frames_per_second != session->configured_audio_frames_per_second) {
    configure_audio_resampler(session, session->last_audio_sample_rate);
  }
}

static void post_audio_buffer(struct mAVStream *stream, struct mAudioBuffer *source) {
  gbaxr_mgba_session *session = (gbaxr_mgba_session *)((char *)stream - offsetof(gbaxr_mgba_session, audio_stream));
  size_t before;
  if (session == NULL || source == NULL || session->audio == NULL || session->audio->ring == NULL) {
    return;
  }
  if (session->audio_resampler.source == NULL) {
    unsigned rate = session->core->audioSampleRate(session->core);
    configure_audio_resampler(session, rate);
  }
  do {
    before = mAudioBufferAvailable(source);
    (void)mAudioResamplerProcess(&session->audio_resampler);
    drain_resampled_audio(session);
  } while (mAudioBufferAvailable(source) < before && mAudioBufferAvailable(source) != 0U);
}

static struct gbaxr_mgba_audio_endpoint *create_audio_endpoint(void) {
  struct gbaxr_mgba_audio_endpoint *endpoint = calloc(1U, sizeof(*endpoint));
  if (endpoint == NULL) {
    return NULL;
  }
  endpoint->ring = gbaxr_audio_ring_create(GBAXR_AUDIO_RING_FRAMES);
  if (endpoint->ring == NULL) {
    free(endpoint);
    return NULL;
  }
  atomic_init(&endpoint->mgba_sample_rate, 0U);
  return endpoint;
}

static void destroy_audio_endpoint(struct gbaxr_mgba_audio_endpoint *endpoint) {
  if (endpoint != NULL) {
    gbaxr_audio_ring_destroy(endpoint->ring);
    free(endpoint);
  }
}

static gbaxr_mgba_session *create_session_with_audio(
    gbaxr_context *bridge,
    struct gbaxr_mgba_audio_endpoint *audio,
    const atomic_uint *frames_per_second) {
  gbaxr_mgba_session *session;
  if (bridge == NULL) {
    return NULL;
  }
  session = (gbaxr_mgba_session *)calloc(1U, sizeof(*session));
  if (session == NULL) {
    return NULL;
  }
  session->bridge = bridge;
  session->audio = audio;
  session->frames_per_second = frames_per_second;
  if (session->audio == NULL || session->audio->ring == NULL) {
    free(session);
    return NULL;
  }
  session->core = GBACoreCreate();
  if (session->core == NULL || !session->core->init(session->core)) {
    free(session);
    return NULL;
  }
  mCoreInitConfig(session->core, "gba-xr");
  /* The core options struct is calloc-initialized, so mCoreLoadConfig would
   * otherwise apply volume=0 and make every mGBA PCM frame silent. Match the
   * audio defaults used by mGBA's SDL/libretro frontends before config map. */
  {
    const struct mCoreOptions defaults = {
      .volume = GBAXR_MGBA_AUDIO_DEFAULT_VOLUME,
      .mute = GBAXR_MGBA_AUDIO_DEFAULT_MUTE,
      .audioSync = GBAXR_MGBA_AUDIO_DEFAULT_SYNC
    };
    mCoreConfigLoadDefaults(&session->core->config, &defaults);
  }
  mCoreLoadConfig(session->core);
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "GbaXr",
                      "mGBA audio config volume=%d mute=%d audioSync=%d",
                      session->core->opts.volume,
                      session->core->opts.mute,
                      session->core->opts.audioSync);
#endif
  session->core->setVideoBuffer(session->core, session->resolved_native, GBAXR_GBA_WIDTH);
  session->core->setAudioBufferSize(session->core, GBAXR_MGBA_AUDIO_BUFFER_FRAMES);
  mAudioBufferInit(&session->resampled_audio, GBAXR_AUDIO_RESAMPLE_BUFFER_FRAMES, 2U);
  mAudioResamplerInit(&session->audio_resampler, mINTERPOLATOR_COSINE);
  mAudioResamplerSetDestination(&session->audio_resampler,
                               &session->resampled_audio,
                               GBAXR_AUDIO_PCM_SAMPLE_RATE);
  memset(&session->audio_stream, 0, sizeof(session->audio_stream));
  session->audio_stream.audioRateChanged = audio_rate_changed;
  session->audio_stream.postAudioBuffer = post_audio_buffer;
  session->core->setAVStream(session->core, &session->audio_stream);
  return session;
}

gbaxr_mgba_session *gbaxr_mgba_create(gbaxr_context *bridge) {
  struct gbaxr_mgba_audio_endpoint *audio = create_audio_endpoint();
  gbaxr_mgba_session *session = create_session_with_audio(bridge, audio, NULL);
  if (session == NULL) {
    destroy_audio_endpoint(audio);
    return NULL;
  }
  session->owns_audio_endpoint = 1;
  return session;
}

void gbaxr_mgba_destroy(gbaxr_mgba_session *session) {
  if (session == NULL) {
    return;
  }
  if (session->core != NULL) {
    session->core->setAVStream(session->core, NULL);
  }
  mAudioResamplerDeinit(&session->audio_resampler);
  mAudioBufferDeinit(&session->resampled_audio);
  if (session->core != NULL) {
    if (session->rom_loaded) {
      session->core->unloadROM(session->core);
    }
    mCoreConfigDeinit(&session->core->config);
    session->core->deinit(session->core);
  }
  if (session->owns_audio_endpoint) {
    destroy_audio_endpoint(session->audio);
  }
  free(session);
}

gbaxr_status gbaxr_mgba_load_rom(gbaxr_mgba_session *session,
                                  const char *utf8_rom_path) {
  struct VFile *rom;
  if (session == NULL || session->core == NULL || utf8_rom_path == NULL || utf8_rom_path[0] == '\0') {
    return GBAXR_INVALID_ARGUMENT;
  }
  if (session->rom_loaded) {
    session->core->unloadROM(session->core);
    session->rom_loaded = 0;
  }
  rom = VFileOpen(utf8_rom_path, O_RDONLY);
  if (rom == NULL) {
    return GBAXR_STATE_ERROR;
  }
  if (!session->core->loadROM(session->core, rom)) {
    rom->close(rom);
    return GBAXR_STATE_ERROR;
  }
  /* Let mGBA create/load the conventional sibling .sav file now that the ROM
   * is in Unity's app-private persistent directory. */
  if (!mCoreAutoloadSave(session->core)) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_WARN, "GbaXr", "could not auto-load save data");
#endif
  }
  session->core->reset(session->core);
  /* Start each loaded ROM with no candidate coverage. The software renderer
   * forces candidate scanlines dirty, and run_frame clears these masks again
   * before every snapshot. */
  memset(session->bg0_candidate_coverage, 0, sizeof(session->bg0_candidate_coverage));
  memset(session->bg1_candidate_coverage, 0, sizeof(session->bg1_candidate_coverage));
  memset(session->obj_candidate_coverage, 0, sizeof(session->obj_candidate_coverage));
  /* mGBA associates its real software renderer during ROM load/reset. Do not
   * cast the pre-load dummy renderer: its smaller layout contains callbacks. */
  GBAVideoSoftwareRendererSetProvenanceBuffer(
      (struct GBAVideoSoftwareRenderer *)((struct GBA *)session->core->board)->video.renderer,
      (uint32_t *)session->provenance,
      GBAXR_GBA_WIDTH);
  GBAVideoSoftwareRendererSetBackgroundCandidateBuffer(
      (struct GBAVideoSoftwareRenderer *)((struct GBA *)session->core->board)->video.renderer,
      0U, session->bg0_candidate_native, session->bg0_candidate_coverage, GBAXR_GBA_WIDTH);
  GBAVideoSoftwareRendererSetBackgroundCandidateBuffer(
      (struct GBAVideoSoftwareRenderer *)((struct GBA *)session->core->board)->video.renderer,
      1U, session->bg1_candidate_native, session->bg1_candidate_coverage, GBAXR_GBA_WIDTH);
  GBAVideoSoftwareRendererSetObjCandidateBuffer(
      (struct GBAVideoSoftwareRenderer *)((struct GBA *)session->core->board)->video.renderer,
      session->obj_candidate_native, session->obj_candidate_coverage, GBAXR_GBA_WIDTH);
  session->rom_loaded = 1;
  return GBAXR_OK;
}

void gbaxr_mgba_unload_rom(gbaxr_mgba_session *session) {
  if (session != NULL && session->core != NULL && session->rom_loaded) {
    session->core->unloadROM(session->core);
    session->rom_loaded = 0;
  }
}

gbaxr_status gbaxr_mgba_run_frame(gbaxr_mgba_session *session) {
  gbaxr_image_view image;
  gbaxr_layer_view layers[GBAXR_MAX_LAYERS];
  uint32_t source;
  uint32_t layer_count = 0U;
  if (session == NULL || session->core == NULL || !session->rom_loaded) {
    return GBAXR_STATE_ERROR;
  }
  memset(session->bg0_candidate_coverage, 0, sizeof(session->bg0_candidate_coverage));
  memset(session->bg1_candidate_coverage, 0, sizeof(session->bg1_candidate_coverage));
  memset(session->obj_candidate_coverage, 0, sizeof(session->obj_candidate_coverage));
  session->core->runFrame(session->core);
  convert_native_to_rgba8(session->resolved_native, session->resolved_rgba);
  convert_native_to_rgba8(session->bg0_candidate_native, session->bg0_candidate_rgba);
  convert_native_to_rgba8(session->bg1_candidate_native, session->bg1_candidate_rgba);
  convert_native_to_rgba8(session->obj_candidate_native, session->obj_candidate_rgba);
  image.rgba8 = session->resolved_rgba;
  image.width = GBAXR_GBA_WIDTH;
  image.height = GBAXR_GBA_HEIGHT;
  image.row_stride_bytes = GBAXR_GBA_WIDTH * 4U;
  /* A raw candidate may be called independent only when it exactly explains
   * every resolved pixel without composition effects. Anything broader falls
   * back to the diagnostic visible partition. */
  {
    uint32_t bg0_priority;
    if (gbaxr_bg0_candidate_recomposes_resolved(session->resolved_rgba, session->provenance,
                                                 session->bg0_candidate_rgba,
                                                 session->bg0_candidate_coverage,
                                                 &bg0_priority)) {
      layers[0].source = GBAXR_SOURCE_BG0;
      layers[0].color.rgba8 = session->bg0_candidate_rgba;
      layers[0].color.width = GBAXR_GBA_WIDTH;
      layers[0].color.height = GBAXR_GBA_HEIGHT;
      layers[0].color.row_stride_bytes = GBAXR_GBA_WIDTH * 4U;
      layers[0].coverage_mask = session->bg0_candidate_coverage;
      layers[0].priority = bg0_priority;
      return gbaxr_publish_frame_layers(session->bridge, &image, session->provenance,
                                        GBAXR_GBA_WIDTH * (uint32_t)sizeof(*session->provenance),
                                        layers, 1U, GBAXR_LAYERS_INDEPENDENT_CANDIDATES);
    }
    if (gbaxr_bg0_bg1_candidates_recompose_resolved(session->resolved_rgba, session->provenance,
                                                     session->bg0_candidate_rgba, session->bg0_candidate_coverage,
                                                     session->bg1_candidate_rgba, session->bg1_candidate_coverage,
                                                     &bg0_priority)) {
      layers[0].source = GBAXR_SOURCE_BG0;
      layers[0].color = image;
      layers[0].color.rgba8 = session->bg0_candidate_rgba;
      layers[0].coverage_mask = session->bg0_candidate_coverage;
      layers[0].priority = bg0_priority;
      layers[1].source = GBAXR_SOURCE_BG1;
      layers[1].color = image;
      layers[1].color.rgba8 = session->bg1_candidate_rgba;
      layers[1].coverage_mask = session->bg1_candidate_coverage;
      layers[1].priority = bg0_priority;
      return gbaxr_publish_frame_layers(session->bridge, &image, session->provenance,
                                        GBAXR_GBA_WIDTH * (uint32_t)sizeof(*session->provenance),
                                        layers, 2U, GBAXR_LAYERS_INDEPENDENT_CANDIDATES);
    }
    if (gbaxr_bg0_obj_candidates_recompose_resolved(session->resolved_rgba, session->provenance,
                                                     session->bg0_candidate_rgba, session->bg0_candidate_coverage,
                                                     session->obj_candidate_rgba, session->obj_candidate_coverage,
                                                     &bg0_priority)) {
      layers[0].source = GBAXR_SOURCE_BG0; layers[0].color = image; layers[0].color.rgba8 = session->bg0_candidate_rgba; layers[0].coverage_mask = session->bg0_candidate_coverage; layers[0].priority = bg0_priority;
      layers[1].source = GBAXR_SOURCE_OBJ; layers[1].color = image; layers[1].color.rgba8 = session->obj_candidate_rgba; layers[1].coverage_mask = session->obj_candidate_coverage; layers[1].priority = bg0_priority;
      return gbaxr_publish_frame_layers(session->bridge, &image, session->provenance, GBAXR_GBA_WIDTH * (uint32_t)sizeof(*session->provenance), layers, 2U, GBAXR_LAYERS_INDEPENDENT_CANDIDATES);
    }
  }
  if (gbaxr_build_visible_partition(session->resolved_rgba, session->provenance,
                                    &session->visible_partition) != 0) {
    return GBAXR_STATE_ERROR;
  }
#if defined(__ANDROID__)
  if (!session->logged_candidate_coverage && (session->provenance[0].source == GBAXR_SOURCE_BG0 ||
      session->provenance[0].source == GBAXR_SOURCE_BG1)) {
    uint32_t pixel, bg0_count = 0U, bg1_count = 0U;
    for (pixel = 0U; pixel < GBAXR_FRAME_PIXELS; ++pixel) {
      bg0_count += session->bg0_candidate_coverage[pixel] != 0U;
      bg1_count += session->bg1_candidate_coverage[pixel] != 0U;
    }
    __android_log_print(ANDROID_LOG_INFO, "GbaXr", "raw candidate coverage: bg0=%u bg1=%u", bg0_count, bg1_count);
    session->logged_candidate_coverage = 1;
  }
#endif
  for (source = 0U; source < GBAXR_MAX_LAYERS; ++source) {
    gbaxr_layer_view *layer;
    if (!session->visible_partition.present[source]) {
      continue;
    }
    layer = &layers[layer_count++];
    layer->source = (gbaxr_source_kind)source;
    layer->color.rgba8 = session->visible_partition.rgba[source];
    layer->color.width = GBAXR_GBA_WIDTH;
    layer->color.height = GBAXR_GBA_HEIGHT;
    layer->color.row_stride_bytes = GBAXR_GBA_WIDTH * 4U;
    layer->coverage_mask = session->visible_partition.coverage[source];
    layer->priority = session->visible_partition.priority[source];
  }
  return gbaxr_publish_frame_layers(session->bridge,
                                    &image,
                                    session->provenance,
                                    GBAXR_GBA_WIDTH * (uint32_t)sizeof(*session->provenance),
                                    layers,
                                    layer_count,
                                    GBAXR_LAYERS_VISIBLE_PARTITION);
}

static uint64_t monotonic_nanoseconds(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static uint64_t frame_period_nanoseconds(uint32_t frames_per_second) {
  return 1000000000ULL / (frames_per_second == 0U ? 60U : frames_per_second);
}

static void sleep_until_deadline(uint64_t deadline_nanoseconds) {
  struct timespec delay;
  uint64_t now = monotonic_nanoseconds();
  if (now >= deadline_nanoseconds) {
    return;
  }
  now = deadline_nanoseconds - now;
  delay.tv_sec = (time_t)(now / 1000000000ULL);
  delay.tv_nsec = (long)(now % 1000000000ULL);
  while (nanosleep(&delay, &delay) != 0) {
    /* Retry an interrupted sleep against its remaining interval. */
  }
}

static void *mgba_worker_main(void *opaque) {
  gbaxr_mgba_worker *worker = (gbaxr_mgba_worker *)opaque;
  uint32_t pacing_frames_per_second;
  uint64_t next_deadline_nanoseconds;
  gbaxr_mgba_session *session = create_session_with_audio(
      worker->bridge, &worker->audio, &worker->frames_per_second);
  if (session == NULL) {
    atomic_store(&worker->state, GBAXR_MGBA_WORKER_INIT_FAILED);
    return NULL;
  }
  if (gbaxr_mgba_load_rom(session, worker->rom_path) != GBAXR_OK) {
    atomic_store(&worker->state, GBAXR_MGBA_WORKER_ROM_LOAD_FAILED);
    gbaxr_mgba_destroy(session);
    return NULL;
  }

  atomic_store(&worker->state, GBAXR_MGBA_WORKER_RUNNING);
  pacing_frames_per_second = atomic_load_explicit(&worker->frames_per_second, memory_order_acquire);
  next_deadline_nanoseconds = monotonic_nanoseconds() + frame_period_nanoseconds(pacing_frames_per_second);
  while (!atomic_load(&worker->stop_requested)) {
    uint32_t requested_frames_per_second = atomic_load_explicit(
        &worker->frames_per_second, memory_order_acquire);
    if (requested_frames_per_second != pacing_frames_per_second) {
      pacing_frames_per_second = requested_frames_per_second;
      refresh_audio_resampler_for_frame_rate(session);
      next_deadline_nanoseconds = monotonic_nanoseconds() +
          frame_period_nanoseconds(pacing_frames_per_second);
    }
    session->core->setKeys(session->core, atomic_load(&worker->keys));
    gbaxr_status status = gbaxr_mgba_run_frame(session);
    if (status != GBAXR_OK && status != GBAXR_NO_FRAME) {
      atomic_store(&worker->state, GBAXR_MGBA_WORKER_FRAME_FAILED);
      break;
    }
    /* State save/load is deliberately performed only after run_frame returns,
     * keeping all mGBA state ownership on this worker and at a frame boundary. */
    process_state_command(worker, session);
    if (monotonic_nanoseconds() >= next_deadline_nanoseconds) {
      /* A slow emulation frame must not create a catch-up burst or drift. */
      next_deadline_nanoseconds = monotonic_nanoseconds() +
          frame_period_nanoseconds(pacing_frames_per_second);
      continue;
    }
    sleep_until_deadline(next_deadline_nanoseconds);
    next_deadline_nanoseconds += frame_period_nanoseconds(pacing_frames_per_second);
  }
  cancel_pending_state_command(worker);
  gbaxr_mgba_destroy(session);
  return NULL;
}

gbaxr_mgba_worker *gbaxr_mgba_worker_start(gbaxr_context *bridge,
                                             const char *utf8_rom_path,
                                             uint32_t frames_per_second) {
  gbaxr_mgba_worker *worker;
  size_t path_length;
  if (bridge == NULL || utf8_rom_path == NULL || utf8_rom_path[0] == '\0' ||
      frames_per_second == 0U) {
    return NULL;
  }
  worker = (gbaxr_mgba_worker *)calloc(1U, sizeof(*worker));
  if (worker == NULL) {
    return NULL;
  }
  path_length = strlen(utf8_rom_path) + 1U;
  worker->rom_path = (char *)malloc(path_length);
  if (worker->rom_path == NULL) {
    free(worker);
    return NULL;
  }
  memcpy(worker->rom_path, utf8_rom_path, path_length);
  worker->bridge = bridge;
  worker->audio.ring = gbaxr_audio_ring_create(GBAXR_AUDIO_RING_FRAMES);
  if (worker->audio.ring == NULL) {
    free(worker->rom_path);
    free(worker);
    return NULL;
  }
  atomic_init(&worker->audio.mgba_sample_rate, 0U);
  atomic_init(&worker->frames_per_second, frames_per_second);
  atomic_init(&worker->stop_requested, 0);
  atomic_init(&worker->state, GBAXR_MGBA_WORKER_STARTING);
  atomic_init(&worker->keys, 0U);
  if (pthread_mutex_init(&worker->state_mutex, NULL) != 0) {
    gbaxr_audio_ring_destroy(worker->audio.ring);
    free(worker->rom_path);
    free(worker);
    return NULL;
  }
  if (pthread_cond_init(&worker->state_complete, NULL) != 0) {
    pthread_mutex_destroy(&worker->state_mutex);
    gbaxr_audio_ring_destroy(worker->audio.ring);
    free(worker->rom_path);
    free(worker);
    return NULL;
  }
  if (pthread_create(&worker->thread, NULL, mgba_worker_main, worker) != 0) {
    pthread_cond_destroy(&worker->state_complete);
    pthread_mutex_destroy(&worker->state_mutex);
    gbaxr_audio_ring_destroy(worker->audio.ring);
    free(worker->rom_path);
    free(worker);
    return NULL;
  }
  worker->thread_started = 1;
  return worker;
}

int gbaxr_mgba_worker_state(const gbaxr_mgba_worker *worker) {
  if (worker == NULL) {
    return GBAXR_MGBA_WORKER_INIT_FAILED;
  }
  return atomic_load(&worker->state);
}

int gbaxr_mgba_worker_set_frames_per_second(gbaxr_mgba_worker *worker,
                                            uint32_t frames_per_second) {
  if (worker == NULL || frames_per_second == 0U || frames_per_second > 120U) {
    return GBAXR_STATE_ERROR;
  }
  atomic_store(&worker->frames_per_second, frames_per_second);
  return GBAXR_OK;
}

void gbaxr_mgba_worker_set_keys(gbaxr_mgba_worker *worker, uint32_t keys) {
  if (worker != NULL) {
    atomic_store(&worker->keys, keys & 0x3FFU);
  }
}

uint32_t gbaxr_mgba_worker_read_audio_pcm_s16(gbaxr_mgba_worker *worker,
                                               int16_t *stereo_interleaved,
                                               uint32_t max_frames) {
  if (worker == NULL || worker->audio.ring == NULL) {
    return 0U;
  }
  return gbaxr_audio_ring_read(worker->audio.ring, stereo_interleaved, max_frames);
}

uint32_t gbaxr_mgba_worker_audio_available_frames(const gbaxr_mgba_worker *worker) {
  return worker == NULL ? 0U : gbaxr_audio_ring_available(worker->audio.ring);
}

uint32_t gbaxr_mgba_worker_audio_pcm_sample_rate(const gbaxr_mgba_worker *worker) {
  return worker != NULL && worker->audio.ring != NULL ? GBAXR_AUDIO_PCM_SAMPLE_RATE : 0U;
}

uint32_t gbaxr_mgba_worker_mgba_audio_sample_rate(const gbaxr_mgba_worker *worker) {
  return worker == NULL ? 0U : atomic_load_explicit(&worker->audio.mgba_sample_rate, memory_order_acquire);
}

uint64_t gbaxr_mgba_worker_audio_dropped_frames(const gbaxr_mgba_worker *worker) {
  return worker == NULL ? 0U : gbaxr_audio_ring_dropped_frames(worker->audio.ring);
}

int gbaxr_mgba_worker_save_state(gbaxr_mgba_worker *worker, const char *utf8_state_path) {
  return request_state_command(worker, GBAXR_STATE_COMMAND_SAVE, utf8_state_path);
}

int gbaxr_mgba_worker_load_state(gbaxr_mgba_worker *worker, const char *utf8_state_path) {
  return request_state_command(worker, GBAXR_STATE_COMMAND_LOAD, utf8_state_path);
}

void gbaxr_mgba_worker_stop(gbaxr_mgba_worker *worker) {
  if (worker != NULL && worker->thread_started) {
    atomic_store(&worker->stop_requested, 1);
    cancel_pending_state_command(worker);
    pthread_join(worker->thread, NULL);
    worker->thread_started = 0;
  }
}

void gbaxr_mgba_worker_destroy(gbaxr_mgba_worker *worker) {
  if (worker != NULL) {
    gbaxr_mgba_worker_stop(worker);
    pthread_cond_destroy(&worker->state_complete);
    pthread_mutex_destroy(&worker->state_mutex);
    gbaxr_audio_ring_destroy(worker->audio.ring);
    free(worker->rom_path);
    free(worker);
  }
}
