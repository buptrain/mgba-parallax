#include "emuxr_gba_libretro_worker.h"

#include "emuxr_gba_bridge_sink.h"
#include "emuxr_pcm_resampler.h"
#include "gba_xr_audio_ring.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

enum {
  EMUXR_AUDIO_RING_FRAMES = 8192U,
  EMUXR_STATE_NONE = 0,
  EMUXR_STATE_SAVE = 1,
  EMUXR_STATE_LOAD = 2,
  EMUXR_STATE_RUNNING = 3
};

struct emuxr_gba_libretro_worker {
#if defined(_WIN32)
  HANDLE thread;
  CRITICAL_SECTION state_mutex;
  CONDITION_VARIABLE state_complete;
#else
  pthread_t thread;
  pthread_mutex_t state_mutex;
  pthread_cond_t state_complete;
#endif
  atomic_int stop_requested;
  atomic_int state;
  atomic_uint_fast16_t joypad_mask;
  atomic_uint source_audio_sample_rate;
  atomic_uint audio_sample_rate;
  emuxr_libretro_api api;
  gbaxr_context *bridge;
  gbaxr_audio_ring *audio_ring;
  emuxr_pcm_resampler audio_resampler;
  char *rom_path;
  uint32_t frames_per_second;
  int state_command;
  int state_result;
  char *state_path;
  int thread_started;
};

static void state_mutex_lock(emuxr_gba_libretro_worker *worker) {
#if defined(_WIN32)
  EnterCriticalSection(&worker->state_mutex);
#else
  pthread_mutex_lock(&worker->state_mutex);
#endif
}

static void state_mutex_unlock(emuxr_gba_libretro_worker *worker) {
#if defined(_WIN32)
  LeaveCriticalSection(&worker->state_mutex);
#else
  pthread_mutex_unlock(&worker->state_mutex);
#endif
}

static void state_wait(emuxr_gba_libretro_worker *worker) {
#if defined(_WIN32)
  SleepConditionVariableCS(&worker->state_complete, &worker->state_mutex, INFINITE);
#else
  pthread_cond_wait(&worker->state_complete, &worker->state_mutex);
#endif
}

static void state_broadcast(emuxr_gba_libretro_worker *worker) {
#if defined(_WIN32)
  WakeAllConditionVariable(&worker->state_complete);
#else
  pthread_cond_broadcast(&worker->state_complete);
#endif
}

static uint64_t monotonic_nanoseconds(void) {
#if defined(_WIN32)
  LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000000ULL +
      (uint64_t)(counter.QuadPart % frequency.QuadPart) * 1000000000ULL /
          (uint64_t)frequency.QuadPart;
#else
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
#endif
}

static uint64_t frame_period_nanoseconds(uint32_t frames_per_second) {
  return 1000000000ULL / (frames_per_second == 0U ? 60U : frames_per_second);
}

/* The core reports nominal audio samples per emulated second, while this
 * worker deliberately runs retro_run at the frontend's fixed cadence. Feed
 * the resampler the resulting callback cadence, not the nominal rate:
 * rate * target_fps / core_fps. Without this correction 60 FPS against
 * mGBA's ~59.7275 FPS slowly overfills the PCM ring, producing lag/drops. */
static int configure_audio_resampler(emuxr_gba_libretro_worker *worker,
                                    double nominal_sample_rate,
                                    double core_frames_per_second) {
  double effective_sample_rate;
  uint32_t effective_sample_rate_rounded;
  if (worker == NULL || nominal_sample_rate <= 0.0 ||
      core_frames_per_second <= 0.0 || nominal_sample_rate > (double)UINT32_MAX) {
    return -1;
  }
  effective_sample_rate = nominal_sample_rate * (double)worker->frames_per_second /
      core_frames_per_second;
  if (effective_sample_rate <= 0.0 || effective_sample_rate > (double)UINT32_MAX) {
    return -1;
  }
  effective_sample_rate_rounded = (uint32_t)(effective_sample_rate + 0.5);
  if (emuxr_pcm_resampler_init(&worker->audio_resampler,
                                effective_sample_rate_rounded,
                                48000U) != 0) {
    return -1;
  }
  atomic_store(&worker->source_audio_sample_rate, effective_sample_rate_rounded);
  atomic_store(&worker->audio_sample_rate, 48000U);
  return 0;
}

static bool worker_environment_fallback(void *opaque, unsigned command, void *data) {
  emuxr_gba_libretro_worker *worker = (emuxr_gba_libretro_worker *)opaque;
  const emuxr_libretro_system_av_info *av_info =
      (const emuxr_libretro_system_av_info *)data;
  if (command != EMUXR_RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO || worker == NULL ||
      av_info == NULL) {
    return false;
  }
  /* mGBA calls this from its audio-rate-changed hook on the worker thread. */
  return configure_audio_resampler(worker, av_info->audio_sample_rate,
                                   av_info->frames_per_second) == 0;
}

static void sleep_until_deadline(uint64_t deadline_nanoseconds) {
  uint64_t now = monotonic_nanoseconds();
  if (now >= deadline_nanoseconds) {
    return;
  }
  now = deadline_nanoseconds - now;
#if defined(_WIN32)
  Sleep((DWORD)((now + 999999ULL) / 1000000ULL));
#else
  {
    struct timespec delay;
  delay.tv_sec = (time_t)(now / 1000000000ULL);
  delay.tv_nsec = (long)(now % 1000000000ULL);
  while (nanosleep(&delay, &delay) != 0) {
  }
  }
#endif
}

static uint32_t write_resampled_audio(void *opaque, const int16_t *stereo, uint32_t frames) {
  emuxr_gba_libretro_worker *worker = (emuxr_gba_libretro_worker *)opaque;
  return worker == NULL ? 0U : gbaxr_audio_ring_write(worker->audio_ring, stereo, frames);
}

static uint32_t write_audio(void *opaque, const int16_t *stereo, uint32_t frames) {
  emuxr_gba_libretro_worker *worker = (emuxr_gba_libretro_worker *)opaque;
  if (worker == NULL) {
    return 0U;
  }
  (void)emuxr_pcm_resampler_process(&worker->audio_resampler, stereo, frames,
                                    write_resampled_audio, worker);
  /* A libretro batch callback reports source frames consumed. The bounded
   * presentation ring may drop its resampled output deliberately, but the
   * core must never retry this source batch and duplicate audio. */
  return frames;
}

static char *copy_string(const char *source) {
  size_t length;
  char *copy;
  if (source == NULL || source[0] == '\0') {
    return NULL;
  }
  length = strlen(source) + 1U;
  copy = (char *)malloc(length);
  if (copy != NULL) {
    memcpy(copy, source, length);
  }
  return copy;
}

static FILE *open_file(const char *path, const char *mode) {
#if defined(_WIN32)
  FILE *file = NULL;
  return fopen_s(&file, path, mode) == 0 ? file : NULL;
#else
  return fopen(path, mode);
#endif
}

static int save_state_to_path(emuxr_libretro_host *host, const char *path) {
  size_t size;
  size_t path_length;
  uint8_t *state;
  char *temporary_path;
  FILE *file;
  int result = GBAXR_STATE_ERROR;
  if (host == NULL || path == NULL || path[0] == '\0') {
    return GBAXR_INVALID_ARGUMENT;
  }
  size = emuxr_libretro_host_state_size(host);
  state = size == 0U ? NULL : (uint8_t *)malloc(size);
  path_length = strlen(path);
  temporary_path = (char *)malloc(path_length + sizeof(".tmp"));
  if (state == NULL || temporary_path == NULL ||
      emuxr_libretro_host_save_state(host, state, size) != 0) {
    free(state);
    free(temporary_path);
    return GBAXR_STATE_ERROR;
  }
  memcpy(temporary_path, path, path_length);
  memcpy(temporary_path + path_length, ".tmp", sizeof(".tmp"));
  file = open_file(temporary_path, "wb");
  if (file != NULL) {
    if (fwrite(state, 1U, size, file) == size && fflush(file) == 0) {
      const int close_result = fclose(file);
      file = NULL;
      if (close_result == 0 && rename(temporary_path, path) == 0) {
        result = GBAXR_OK;
      }
    }
    if (file != NULL) {
      fclose(file);
    }
  }
  if (result != GBAXR_OK) {
    remove(temporary_path);
  }
  free(state);
  free(temporary_path);
  return result;
}

static int load_state_from_path(emuxr_libretro_host *host, const char *path) {
  size_t expected;
  long file_size;
  uint8_t *state;
  FILE *file;
  int result;
  if (host == NULL || path == NULL || path[0] == '\0') {
    return GBAXR_INVALID_ARGUMENT;
  }
  expected = emuxr_libretro_host_state_size(host);
  file = open_file(path, "rb");
  if (expected == 0U || file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
      (file_size = ftell(file)) < 0 || (size_t)file_size != expected ||
      fseek(file, 0L, SEEK_SET) != 0) {
    if (file != NULL) {
      fclose(file);
    }
    return GBAXR_STATE_ERROR;
  }
  state = (uint8_t *)malloc(expected);
  if (state == NULL) {
    fclose(file);
    return GBAXR_STATE_ERROR;
  }
  {
    const int read_succeeded = fread(state, 1U, expected, file) == expected;
    const int close_succeeded = fclose(file) == 0;
    result = read_succeeded && close_succeeded &&
        emuxr_libretro_host_load_state(host, state, expected) == 0 ?
        GBAXR_OK : GBAXR_STATE_ERROR;
  }
  free(state);
  return result;
}

static void process_state_command(emuxr_gba_libretro_worker *worker,
                                  emuxr_libretro_host *host) {
  int command;
  char *path;
  int result;
  state_mutex_lock(worker);
  command = worker->state_command;
  if (command == EMUXR_STATE_NONE) {
    state_mutex_unlock(worker);
    return;
  }
  path = worker->state_path;
  worker->state_path = NULL;
  worker->state_command = EMUXR_STATE_RUNNING;
  state_mutex_unlock(worker);

  result = command == EMUXR_STATE_SAVE ? save_state_to_path(host, path) : load_state_from_path(host, path);
  free(path);

  state_mutex_lock(worker);
  worker->state_result = result;
  worker->state_command = EMUXR_STATE_NONE;
  state_broadcast(worker);
  state_mutex_unlock(worker);
}

static void cancel_state_command(emuxr_gba_libretro_worker *worker) {
  state_mutex_lock(worker);
  free(worker->state_path);
  worker->state_path = NULL;
  worker->state_result = GBAXR_STATE_ERROR;
  worker->state_command = EMUXR_STATE_NONE;
  state_broadcast(worker);
  state_mutex_unlock(worker);
}

static int request_state_command(emuxr_gba_libretro_worker *worker,
                                 int command,
                                 const char *path) {
  char *path_copy = copy_string(path);
  int result;
  if (worker == NULL || path_copy == NULL) {
    return GBAXR_INVALID_ARGUMENT;
  }
  state_mutex_lock(worker);
  if (atomic_load(&worker->state) != EMUXR_LIBRETRO_WORKER_RUNNING ||
      worker->state_command != EMUXR_STATE_NONE) {
    state_mutex_unlock(worker);
    free(path_copy);
    return GBAXR_STATE_ERROR;
  }
  worker->state_path = path_copy;
  worker->state_result = GBAXR_STATE_ERROR;
  worker->state_command = command;
  while (worker->state_command != EMUXR_STATE_NONE && !atomic_load(&worker->stop_requested)) {
    state_wait(worker);
  }
  result = worker->state_command == EMUXR_STATE_NONE ? worker->state_result : GBAXR_STATE_ERROR;
  state_mutex_unlock(worker);
  return result;
}

static void worker_main_body(emuxr_gba_libretro_worker *worker) {
  emuxr_libretro_host_config config;
  emuxr_libretro_game_info game;
  emuxr_libretro_system_av_info av_info;
  emuxr_libretro_host *host;
  uint64_t next_deadline;

  memset(&config, 0, sizeof(config));
  config.video_sink = emuxr_gba_bridge_sink_publish;
  config.video_sink_opaque = worker->bridge;
  config.audio_sink = write_audio;
  config.audio_sink_opaque = worker;
  config.environment_fallback = worker_environment_fallback;
  config.environment_fallback_opaque = worker;
  host = emuxr_libretro_host_create(&worker->api, &config);
  if (host == NULL) {
    atomic_store(&worker->state, EMUXR_LIBRETRO_WORKER_INIT_FAILED);
    return;
  }
  memset(&game, 0, sizeof(game));
  game.path = worker->rom_path;
  if (emuxr_libretro_host_load_game(host, &game) != 0) {
    atomic_store(&worker->state, EMUXR_LIBRETRO_WORKER_ROM_LOAD_FAILED);
    emuxr_libretro_host_destroy(host);
    return;
  }
  if (emuxr_libretro_host_get_system_av_info(host, &av_info) != 0 ||
      av_info.base_width != GBAXR_GBA_WIDTH || av_info.base_height != GBAXR_GBA_HEIGHT ||
      configure_audio_resampler(worker, av_info.audio_sample_rate,
                                av_info.frames_per_second) != 0) {
    atomic_store(&worker->state, EMUXR_LIBRETRO_WORKER_UNSUPPORTED_VIDEO);
    emuxr_libretro_host_destroy(host);
    return;
  }
  atomic_store(&worker->state, EMUXR_LIBRETRO_WORKER_RUNNING);
  next_deadline = monotonic_nanoseconds() + frame_period_nanoseconds(worker->frames_per_second);
  while (!atomic_load(&worker->stop_requested)) {
    const uint64_t rejected_before = emuxr_libretro_host_rejected_video_frame_count(host);
    emuxr_libretro_host_set_joypad_mask(host, 0U,
        (uint16_t)atomic_load_explicit(&worker->joypad_mask, memory_order_acquire));
    if (emuxr_libretro_host_run_frame(host) != 0 ||
        emuxr_libretro_host_rejected_video_frame_count(host) != rejected_before) {
      atomic_store(&worker->state, EMUXR_LIBRETRO_WORKER_FRAME_FAILED);
      break;
    }
    process_state_command(worker, host);
    if (monotonic_nanoseconds() >= next_deadline) {
      next_deadline = monotonic_nanoseconds() + frame_period_nanoseconds(worker->frames_per_second);
    } else {
      sleep_until_deadline(next_deadline);
      next_deadline += frame_period_nanoseconds(worker->frames_per_second);
    }
  }
  cancel_state_command(worker);
  emuxr_libretro_host_destroy(host);
}

#if defined(_WIN32)
static DWORD WINAPI worker_main(LPVOID opaque) {
  worker_main_body((emuxr_gba_libretro_worker *)opaque);
  return 0U;
}
#else
static void *worker_main(void *opaque) {
  worker_main_body((emuxr_gba_libretro_worker *)opaque);
  return NULL;
}
#endif

emuxr_gba_libretro_worker *emuxr_gba_libretro_worker_start(
    gbaxr_context *bridge,
    const emuxr_libretro_api *api,
    const char *utf8_rom_path,
    uint32_t frames_per_second) {
  emuxr_gba_libretro_worker *worker;
  if (bridge == NULL || api == NULL || utf8_rom_path == NULL || utf8_rom_path[0] == '\0' ||
      frames_per_second == 0U || frames_per_second > 120U) {
    return NULL;
  }
  worker = (emuxr_gba_libretro_worker *)calloc(1U, sizeof(*worker));
  if (worker == NULL) {
    return NULL;
  }
  worker->rom_path = copy_string(utf8_rom_path);
  worker->audio_ring = gbaxr_audio_ring_create(EMUXR_AUDIO_RING_FRAMES);
  if (worker->rom_path == NULL || worker->audio_ring == NULL) {
    if (worker->audio_ring != NULL) gbaxr_audio_ring_destroy(worker->audio_ring);
    free(worker->rom_path);
    free(worker);
    return NULL;
  }
#if defined(_WIN32)
  InitializeCriticalSection(&worker->state_mutex);
  InitializeConditionVariable(&worker->state_complete);
#else
  if (pthread_mutex_init(&worker->state_mutex, NULL) != 0) {
    gbaxr_audio_ring_destroy(worker->audio_ring);
    free(worker->rom_path);
    free(worker);
    return NULL;
  }
  if (pthread_cond_init(&worker->state_complete, NULL) != 0) {
    pthread_mutex_destroy(&worker->state_mutex);
    gbaxr_audio_ring_destroy(worker->audio_ring);
    free(worker->rom_path);
    free(worker);
    return NULL;
  }
#endif
  worker->api = *api;
  worker->bridge = bridge;
  worker->frames_per_second = frames_per_second;
  atomic_init(&worker->stop_requested, 0);
  atomic_init(&worker->state, EMUXR_LIBRETRO_WORKER_STARTING);
  atomic_init(&worker->joypad_mask, 0U);
  atomic_init(&worker->source_audio_sample_rate, 0U);
  atomic_init(&worker->audio_sample_rate, 0U);
#if defined(_WIN32)
  worker->thread = CreateThread(NULL, 0U, worker_main, worker, 0U, NULL);
  if (worker->thread == NULL) {
    DeleteCriticalSection(&worker->state_mutex);
    gbaxr_audio_ring_destroy(worker->audio_ring);
    free(worker->rom_path);
    free(worker);
    return NULL;
  }
#else
  if (pthread_create(&worker->thread, NULL, worker_main, worker) != 0) {
    pthread_cond_destroy(&worker->state_complete);
    pthread_mutex_destroy(&worker->state_mutex);
    gbaxr_audio_ring_destroy(worker->audio_ring);
    free(worker->rom_path);
    free(worker);
    return NULL;
  }
#endif
  worker->thread_started = 1;
  return worker;
}

int emuxr_gba_libretro_worker_state(const emuxr_gba_libretro_worker *worker) {
  return worker == NULL ? EMUXR_LIBRETRO_WORKER_INIT_FAILED : atomic_load(&worker->state);
}

void emuxr_gba_libretro_worker_set_joypad_mask(emuxr_gba_libretro_worker *worker,
                                               uint16_t mask) {
  if (worker != NULL) atomic_store(&worker->joypad_mask, mask);
}

uint32_t emuxr_gba_libretro_worker_read_audio_pcm_s16(
    emuxr_gba_libretro_worker *worker,
    int16_t *stereo_interleaved,
    uint32_t max_frames) {
  return worker == NULL ? 0U : gbaxr_audio_ring_read(worker->audio_ring, stereo_interleaved, max_frames);
}

uint32_t emuxr_gba_libretro_worker_audio_available_frames(
    const emuxr_gba_libretro_worker *worker) {
  return worker == NULL ? 0U : gbaxr_audio_ring_available(worker->audio_ring);
}

uint32_t emuxr_gba_libretro_worker_audio_sample_rate(
    const emuxr_gba_libretro_worker *worker) {
  return worker == NULL ? 0U : atomic_load(&worker->audio_sample_rate);
}

uint32_t emuxr_gba_libretro_worker_source_audio_sample_rate(
    const emuxr_gba_libretro_worker *worker) {
  return worker == NULL ? 0U : atomic_load(&worker->source_audio_sample_rate);
}

uint64_t emuxr_gba_libretro_worker_audio_dropped_frames(
    const emuxr_gba_libretro_worker *worker) {
  return worker == NULL ? 0U : gbaxr_audio_ring_dropped_frames(worker->audio_ring);
}

int emuxr_gba_libretro_worker_save_state(emuxr_gba_libretro_worker *worker,
                                         const char *utf8_state_path) {
  return request_state_command(worker, EMUXR_STATE_SAVE, utf8_state_path);
}

int emuxr_gba_libretro_worker_load_state(emuxr_gba_libretro_worker *worker,
                                         const char *utf8_state_path) {
  return request_state_command(worker, EMUXR_STATE_LOAD, utf8_state_path);
}

void emuxr_gba_libretro_worker_stop(emuxr_gba_libretro_worker *worker) {
  if (worker != NULL && worker->thread_started) {
    atomic_store(&worker->stop_requested, 1);
    cancel_state_command(worker);
#if defined(_WIN32)
    WaitForSingleObject(worker->thread, INFINITE);
    CloseHandle(worker->thread);
#else
    pthread_join(worker->thread, NULL);
#endif
    worker->thread_started = 0;
  }
}

void emuxr_gba_libretro_worker_destroy(emuxr_gba_libretro_worker *worker) {
  if (worker != NULL) {
    emuxr_gba_libretro_worker_stop(worker);
#if defined(_WIN32)
    DeleteCriticalSection(&worker->state_mutex);
#else
    pthread_cond_destroy(&worker->state_complete);
    pthread_mutex_destroy(&worker->state_mutex);
#endif
    gbaxr_audio_ring_destroy(worker->audio_ring);
    free(worker->rom_path);
    free(worker);
  }
}
