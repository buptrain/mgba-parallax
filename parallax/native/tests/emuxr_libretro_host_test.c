#include "emuxr_libretro_host.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; } } while (0)

typedef struct fake_core {
  emuxr_libretro_environment_t environment;
  emuxr_libretro_video_refresh_t video;
  emuxr_libretro_audio_sample_t audio;
  emuxr_libretro_audio_sample_batch_t audio_batch;
  emuxr_libretro_input_poll_t input_poll;
  emuxr_libretro_input_state_t input_state;
  int initialized;
  int loaded;
  int unloaded;
  int deinitialized;
  int saw_b;
  int16_t joypad_mask;
  int use_bad_pitch;
  int use_hardware_frame;
  int use_mgba_rgb565;
  int callback_error;
  emuxr_libretro_pixel_format requested_format;
  uint8_t state[4];
} fake_core;

typedef struct captured_output {
  uint8_t rgba[2U * 4U];
  uint32_t width;
  uint32_t height;
  uint32_t audio_frames;
  int fallback_calls;
} captured_output;

static fake_core core;

static void set_environment(emuxr_libretro_environment_t callback) { core.environment = callback; }
static void set_video(emuxr_libretro_video_refresh_t callback) { core.video = callback; }
static void set_audio(emuxr_libretro_audio_sample_t callback) { core.audio = callback; }
static void set_audio_batch(emuxr_libretro_audio_sample_batch_t callback) { core.audio_batch = callback; }
static void set_input_poll(emuxr_libretro_input_poll_t callback) { core.input_poll = callback; }
static void set_input_state(emuxr_libretro_input_state_t callback) { core.input_state = callback; }
static void fake_init(void) { core.initialized = 1; }
static void fake_deinit(void) { core.deinitialized = 1; }
static bool fake_load_game(const emuxr_libretro_game_info *game) {
  core.loaded = game != NULL && game->path != NULL && strcmp(game->path, "fixture.gba") == 0;
  return core.loaded != 0;
}
static void fake_unload_game(void) { core.unloaded = 1; core.loaded = 0; }
static void fake_get_system_av_info(emuxr_libretro_system_av_info *out_info) {
  memset(out_info, 0, sizeof(*out_info));
  out_info->base_width = 2U;
  out_info->base_height = 1U;
  out_info->max_width = 2U;
  out_info->max_height = 1U;
  out_info->aspect_ratio = 2.0f;
  out_info->frames_per_second = 60.0;
  out_info->audio_sample_rate = 48000.0;
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
  static const uint32_t xrgb[2] = { 0x00112233U, 0x00A0B0C0U };
  static const uint16_t rgb565[2] = { 0xF800U, 0x07E0U };
  /* mGBA's BGR555 cyan/yellow after its internal RGB565 conversion. */
  static const uint16_t mgba_rgb565[2] = { 0x07DFU, 0xFFC0U };
  static const uint16_t rgb1555[2] = { 0x7C00U, 0x03E0U };
  static const int16_t audio[] = { 1, -1, 2, -2 };
  bool can_dupe = false;
  if (core.environment == NULL || core.video == NULL || core.audio_batch == NULL ||
      core.input_poll == NULL || core.input_state == NULL ||
      !core.environment(EMUXR_RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &core.requested_format) ||
      !core.environment(EMUXR_RETRO_ENVIRONMENT_GET_CAN_DUPE, &can_dupe) || !can_dupe ||
      !core.environment(EMUXR_RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL) ||
      !core.environment(999U, NULL)) {
    core.callback_error = 1;
    return;
  }
  core.input_poll();
  core.saw_b = core.input_state(0U, EMUXR_RETRO_DEVICE_JOYPAD, 0U,
                                EMUXR_RETRO_DEVICE_ID_JOYPAD_B);
  core.joypad_mask = core.input_state(0U, EMUXR_RETRO_DEVICE_JOYPAD, 0U,
                                      EMUXR_RETRO_DEVICE_ID_JOYPAD_MASK);
  if (core.audio_batch(audio, 2U) != 2U) {
    core.callback_error = 1;
    return;
  }
  if (core.use_hardware_frame) {
    core.video((const void *)(uintptr_t)UINTPTR_MAX, 2U, 1U, 8U);
  } else if (core.requested_format == EMUXR_RETRO_PIXEL_FORMAT_XRGB8888) {
    core.video(xrgb, 2U, 1U, core.use_bad_pitch ? 4U : 8U);
  } else if (core.requested_format == EMUXR_RETRO_PIXEL_FORMAT_RGB565) {
    core.video(core.use_mgba_rgb565 ? mgba_rgb565 : rgb565,
               2U, 1U, core.use_bad_pitch ? 2U : 4U);
  } else {
    core.video(rgb1555, 2U, 1U, core.use_bad_pitch ? 2U : 4U);
  }
}

static int capture_video(void *opaque, const emuxr_rgba_frame *frame) {
  captured_output *captured = (captured_output *)opaque;
  if (frame == NULL || frame->width != 2U || frame->height != 1U ||
      frame->row_stride_bytes != 8U) return -1;
  memcpy(captured->rgba, frame->rgba8, sizeof(captured->rgba));
  captured->width = frame->width;
  captured->height = frame->height;
  return 0;
}

static uint32_t capture_audio(void *opaque, const int16_t *stereo, uint32_t frames) {
  captured_output *captured = (captured_output *)opaque;
  if (frames == 2U && stereo[0] == 1 && stereo[3] == -2) captured->audio_frames += frames;
  return frames;
}

static bool fallback_environment(void *opaque, unsigned command, void *data) {
  captured_output *captured = (captured_output *)opaque;
  (void)command;
  (void)data;
  ++captured->fallback_calls;
  return true;
}

int main(void) {
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
  captured_output captured;
  emuxr_libretro_host_config config;
  emuxr_libretro_game_info game = { "fixture.gba", NULL, 0U, NULL };
  emuxr_libretro_host *host;
  emuxr_libretro_system_av_info av_info;
  uint8_t state[4] = { 9U, 8U, 7U, 6U };

  memset(&core, 0, sizeof(core));
  memset(&captured, 0, sizeof(captured));
  memset(&config, 0, sizeof(config));
  config.video_sink = capture_video;
  config.video_sink_opaque = &captured;
  config.audio_sink = capture_audio;
  config.audio_sink_opaque = &captured;
  config.environment_fallback = fallback_environment;
  config.environment_fallback_opaque = &captured;
  host = emuxr_libretro_host_create(&api, &config);
  CHECK(host != NULL);
  CHECK(core.initialized);
  CHECK(emuxr_libretro_host_run_frame(host) == -1);
  CHECK(emuxr_libretro_host_load_game(host, &game) == 0);
  CHECK(emuxr_libretro_host_get_system_av_info(host, &av_info) == 0);
  CHECK(av_info.base_width == 2U && av_info.base_height == 1U);
  CHECK(av_info.audio_sample_rate == 48000.0);
  emuxr_libretro_host_set_joypad_mask(host, 0U, 1U << EMUXR_RETRO_DEVICE_ID_JOYPAD_B);

  core.requested_format = EMUXR_RETRO_PIXEL_FORMAT_XRGB8888;
  CHECK(emuxr_libretro_host_run_frame(host) == 0);
  CHECK(core.saw_b == 1);
  CHECK(core.joypad_mask == (1U << EMUXR_RETRO_DEVICE_ID_JOYPAD_B));
  CHECK(core.callback_error == 0);
  CHECK(captured.width == 2U && captured.height == 1U);
  CHECK(captured.rgba[0] == 0x11U && captured.rgba[1] == 0x22U && captured.rgba[2] == 0x33U && captured.rgba[3] == 0xFFU);
  CHECK(captured.rgba[4] == 0xA0U && captured.rgba[5] == 0xB0U && captured.rgba[6] == 0xC0U && captured.rgba[7] == 0xFFU);
  CHECK(captured.audio_frames == 2U);
  CHECK(captured.fallback_calls == 1);
  CHECK(emuxr_libretro_host_video_frame_count(host) == 1U);

  core.requested_format = EMUXR_RETRO_PIXEL_FORMAT_RGB565;
  CHECK(emuxr_libretro_host_run_frame(host) == 0);
  CHECK(captured.rgba[0] == 0xFFU && captured.rgba[1] == 0U && captured.rgba[2] == 0U);
  CHECK(captured.rgba[4] == 0U && captured.rgba[5] == 0xFFU && captured.rgba[6] == 0U);

  core.requested_format = EMUXR_RETRO_PIXEL_FORMAT_0RGB1555;
  CHECK(emuxr_libretro_host_run_frame(host) == 0);
  CHECK(captured.rgba[0] == 0xFFU && captured.rgba[1] == 0U && captured.rgba[2] == 0U);
  CHECK(captured.rgba[4] == 0U && captured.rgba[5] == 0xFFU && captured.rgba[6] == 0U);

  core.use_bad_pitch = 1;
  CHECK(emuxr_libretro_host_run_frame(host) == 0);
  CHECK(emuxr_libretro_host_rejected_video_frame_count(host) == 1U);
  core.use_bad_pitch = 0;
  core.use_hardware_frame = 1;
  CHECK(emuxr_libretro_host_run_frame(host) == 0);
  CHECK(emuxr_libretro_host_rejected_video_frame_count(host) == 2U);
  CHECK(emuxr_libretro_host_pixel_format(host) == EMUXR_RETRO_PIXEL_FORMAT_0RGB1555);

  core.state[0] = 1U;
  CHECK(emuxr_libretro_host_state_size(host) == sizeof(state));
  CHECK(emuxr_libretro_host_save_state(host, state, sizeof(state)) == 0);
  CHECK(state[0] == 1U);
  state[0] = 42U;
  CHECK(emuxr_libretro_host_load_state(host, state, sizeof(state)) == 0);
  CHECK(core.state[0] == 42U);
  CHECK(emuxr_libretro_host_save_state(host, state, sizeof(state) - 1U) == -1);

  emuxr_libretro_host_destroy(host);
  CHECK(core.unloaded && core.deinitialized);

  /* This is explicitly mGBA-only. Generic RGB565 above still expands its
   * six-bit green channel; mGBA's 5:5:5-derived RGB565 must restore its
   * source green precision without a red/blue swap. */
  memset(&core, 0, sizeof(core));
  memset(&captured, 0, sizeof(captured));
  api.rgb565_conversion = EMUXR_LIBRETRO_RGB565_MGBA_GBA555;
  config.video_sink_opaque = &captured;
  host = emuxr_libretro_host_create(&api, &config);
  CHECK(host != NULL);
  CHECK(emuxr_libretro_host_load_game(host, &game) == 0);
  core.requested_format = EMUXR_RETRO_PIXEL_FORMAT_RGB565;
  core.use_mgba_rgb565 = 1;
  CHECK(emuxr_libretro_host_run_frame(host) == 0);
  /* Cyan stays cyan and yellow stays yellow; BGR interpretation would swap
   * the red/blue components and fail both checks. */
  CHECK(captured.rgba[0] == 0U && captured.rgba[1] == 0xFFU &&
        captured.rgba[2] == 0xFFU);
  CHECK(captured.rgba[4] == 0xFFU && captured.rgba[5] == 0xFFU &&
        captured.rgba[6] == 0U);
  emuxr_libretro_host_destroy(host);
  CHECK(core.unloaded && core.deinitialized);
  puts("libretro host tests passed");
  return 0;
}
