#include "emuxr_mgba_libretro_provider.h"

/* This header ships with the pinned mGBA source and defines the canonical
 * libretro entry point signatures. This translation unit is compiled only by
 * GBAXR_BUILD_MGBA_LIBRETRO_CORE, after mGBA has been added to the build. */
#include "libretro.h"

static void set_environment(emuxr_libretro_environment_t callback) {
  retro_set_environment((retro_environment_t)callback);
}

static void set_video_refresh(emuxr_libretro_video_refresh_t callback) {
  retro_set_video_refresh((retro_video_refresh_t)callback);
}

static void set_audio_sample(emuxr_libretro_audio_sample_t callback) {
  retro_set_audio_sample((retro_audio_sample_t)callback);
}

static void set_audio_sample_batch(emuxr_libretro_audio_sample_batch_t callback) {
  retro_set_audio_sample_batch((retro_audio_sample_batch_t)callback);
}

static void set_input_poll(emuxr_libretro_input_poll_t callback) {
  retro_set_input_poll((retro_input_poll_t)callback);
}

static void set_input_state(emuxr_libretro_input_state_t callback) {
  retro_set_input_state((retro_input_state_t)callback);
}

static bool load_game(const emuxr_libretro_game_info *game) {
  struct retro_game_info core_game;
  if (game == NULL) {
    return false;
  }
  core_game.path = game->path;
  core_game.data = game->data;
  core_game.size = game->size;
  core_game.meta = game->meta;
  return retro_load_game(&core_game);
}

static void get_system_av_info(emuxr_libretro_system_av_info *out_info) {
  struct retro_system_av_info core_info;
  if (out_info == NULL) {
    return;
  }
  retro_get_system_av_info(&core_info);
  out_info->base_width = core_info.geometry.base_width;
  out_info->base_height = core_info.geometry.base_height;
  out_info->max_width = core_info.geometry.max_width;
  out_info->max_height = core_info.geometry.max_height;
  out_info->aspect_ratio = core_info.geometry.aspect_ratio;
  out_info->frames_per_second = core_info.timing.fps;
  out_info->audio_sample_rate = core_info.timing.sample_rate;
}

static const emuxr_libretro_api api = {
  .set_environment = set_environment,
  .set_video_refresh = set_video_refresh,
  .set_audio_sample = set_audio_sample,
  .set_audio_sample_batch = set_audio_sample_batch,
  .set_input_poll = set_input_poll,
  .set_input_state = set_input_state,
  .init = retro_init,
  .deinit = retro_deinit,
  .load_game = load_game,
  .unload_game = retro_unload_game,
  .run = retro_run,
  .get_system_av_info = get_system_av_info,
  .serialize_size = retro_serialize_size,
  .serialize = retro_serialize,
  .unserialize = retro_unserialize,
  /* mGBA requests standard RGB565, with a sixth green bit duplicated from
   * its source GBA 5-bit palette representation. */
  .rgb565_conversion = EMUXR_LIBRETRO_RGB565_MGBA_GBA555
};

const emuxr_libretro_api *emuxr_mgba_libretro_api(void) {
  return &api;
}
