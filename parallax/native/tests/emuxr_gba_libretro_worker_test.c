#include "emuxr_gba_libretro_worker.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static void sleep_milliseconds(unsigned milliseconds) { Sleep(milliseconds); }
#else
#include <time.h>
static void sleep_milliseconds(unsigned milliseconds) {
  struct timespec delay;
  delay.tv_sec = (time_t)(milliseconds / 1000U);
  delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
  nanosleep(&delay, NULL);
}
#endif

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; } } while (0)

typedef struct fake_core {
  emuxr_libretro_environment_t environment;
  emuxr_libretro_video_refresh_t video;
  emuxr_libretro_audio_sample_batch_t audio_batch;
  emuxr_libretro_input_poll_t input_poll;
  emuxr_libretro_input_state_t input_state;
  atomic_uint run_count;
  atomic_int saw_b;
  uint8_t state[4];
} fake_core;

static fake_core core;
static uint16_t pixels[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];

static void set_environment(emuxr_libretro_environment_t callback) { core.environment = callback; }
static void set_video(emuxr_libretro_video_refresh_t callback) { core.video = callback; }
static void set_audio(emuxr_libretro_audio_sample_t callback) { (void)callback; }
static void set_audio_batch(emuxr_libretro_audio_sample_batch_t callback) { core.audio_batch = callback; }
static void set_input_poll(emuxr_libretro_input_poll_t callback) { core.input_poll = callback; }
static void set_input_state(emuxr_libretro_input_state_t callback) { core.input_state = callback; }
static void fake_init(void) {}
static void fake_deinit(void) {}
static bool fake_load_game(const emuxr_libretro_game_info *game) {
  return game != NULL && game->path != NULL && strcmp(game->path, "fixture.gba") == 0;
}
static void fake_unload_game(void) {}
static void fake_get_system_av_info(emuxr_libretro_system_av_info *out_info) {
  memset(out_info, 0, sizeof(*out_info));
  out_info->base_width = GBAXR_GBA_WIDTH;
  out_info->base_height = GBAXR_GBA_HEIGHT;
  out_info->max_width = GBAXR_GBA_WIDTH;
  out_info->max_height = GBAXR_GBA_HEIGHT;
  out_info->frames_per_second = 59.7275;
  out_info->audio_sample_rate = 32768.0;
}
static size_t fake_serialize_size(void) { return sizeof(core.state); }
static bool fake_serialize(void *data, size_t size) {
  if (data == NULL || size != sizeof(core.state)) return false;
  memcpy(data, core.state, sizeof(core.state));
  return true;
}
static bool fake_unserialize(const void *data, size_t size) {
  if (data == NULL || size != sizeof(core.state)) return false;
  memcpy(core.state, data, sizeof(core.state));
  return true;
}
static void fake_run(void) {
  static const int16_t audio[] = { 100, -100, 200, -200 };
  emuxr_libretro_pixel_format format = EMUXR_RETRO_PIXEL_FORMAT_RGB565;
  if (core.environment == NULL || core.video == NULL || core.audio_batch == NULL ||
      core.input_poll == NULL || core.input_state == NULL ||
      !core.environment(EMUXR_RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)) {
    return;
  }
  core.input_poll();
  atomic_store(&core.saw_b, core.input_state(0U, EMUXR_RETRO_DEVICE_JOYPAD, 0U,
      EMUXR_RETRO_DEVICE_ID_JOYPAD_B));
  pixels[0] = 0x07FFU;
  core.video(pixels, GBAXR_GBA_WIDTH, GBAXR_GBA_HEIGHT, GBAXR_GBA_WIDTH * sizeof(uint16_t));
  core.audio_batch(audio, 2U);
  atomic_fetch_add(&core.run_count, 1U);
}

static int wait_for_running(emuxr_gba_libretro_worker *worker) {
  unsigned attempt;
  for (attempt = 0U; attempt < 200U; ++attempt) {
    if (emuxr_gba_libretro_worker_state(worker) == EMUXR_LIBRETRO_WORKER_RUNNING &&
        atomic_load(&core.run_count) > 0U) return 1;
    sleep_milliseconds(1U);
  }
  return 0;
}

int main(void) {
  const char *state_path = "emuxr_gba_libretro_worker_test.state";
  emuxr_libretro_api api = {
    .set_environment = set_environment,
    .set_video_refresh = set_video,
    .set_audio_sample = set_audio,
    .set_audio_sample_batch = set_audio_batch,
    .set_input_poll = set_input_poll,
    .set_input_state = set_input_state,
    .init = fake_init,
    .deinit = fake_deinit,
    .load_game = fake_load_game,
    .unload_game = fake_unload_game,
    .run = fake_run,
    .get_system_av_info = fake_get_system_av_info,
    .serialize_size = fake_serialize_size,
    .serialize = fake_serialize,
    .unserialize = fake_unserialize
  };
  gbaxr_context *bridge;
  emuxr_gba_libretro_worker *worker;
  gbaxr_frame_lease lease;
  int16_t audio[2U * 2U];

  memset(&core, 0, sizeof(core));
  memset(pixels, 0, sizeof(pixels));
  core.state[0] = 17U;
  remove(state_path);
  bridge = gbaxr_create_unbound(GBAXR_ABI_VERSION);
  CHECK(bridge != NULL);
  worker = emuxr_gba_libretro_worker_start(bridge, &api, "fixture.gba", 60U);
  CHECK(worker != NULL);
  emuxr_gba_libretro_worker_set_joypad_mask(worker,
      (uint16_t)(1U << EMUXR_RETRO_DEVICE_ID_JOYPAD_B));
  CHECK(wait_for_running(worker));
  CHECK(emuxr_gba_libretro_worker_audio_sample_rate(worker) == 48000U);
  /* 32768 Hz nominal at 59.7275 core FPS, paced at 60 FPS. */
  CHECK(emuxr_gba_libretro_worker_source_audio_sample_rate(worker) == 32918U);
  CHECK(atomic_load(&core.saw_b) == 1);
  CHECK(emuxr_gba_libretro_worker_audio_available_frames(worker) >= 2U);
  CHECK(emuxr_gba_libretro_worker_read_audio_pcm_s16(worker, audio, 2U) == 2U);
  CHECK(audio[0] == 100 && audio[1] == -100);
  CHECK(gbaxr_acquire_latest_frame(bridge, &lease) == GBAXR_OK);
  CHECK(lease.resolved_rgba.rgba8[0] == 0U && lease.resolved_rgba.rgba8[1] == 255U &&
      lease.resolved_rgba.rgba8[2] == 255U && lease.resolved_rgba.rgba8[3] == 255U);
  CHECK(gbaxr_release_frame(bridge, &lease) == GBAXR_OK);
  CHECK(emuxr_gba_libretro_worker_save_state(worker, state_path) == GBAXR_OK);
  core.state[0] = 99U;
  CHECK(emuxr_gba_libretro_worker_load_state(worker, state_path) == GBAXR_OK);
  CHECK(core.state[0] == 17U);
  emuxr_gba_libretro_worker_destroy(worker);
  gbaxr_destroy(bridge);
  remove(state_path);
  puts("GBA libretro worker tests passed");
  return 0;
}
