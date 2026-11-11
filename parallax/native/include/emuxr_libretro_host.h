#ifndef EMUXR_LIBRETRO_HOST_H
#define EMUXR_LIBRETRO_HOST_H

/*
 * Small, dependency-free libretro frontend boundary.
 *
 * This intentionally mirrors only the stable libretro C ABI used by the
 * host. It does not vendor a core's private headers or expose PPU semantics.
 * Enhanced composition remains an optional, core-specific extension above
 * this resolved-frame transport.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  EMUXR_RETRO_ENVIRONMENT_GET_CAN_DUPE = 3U,
  EMUXR_RETRO_ENVIRONMENT_SET_PIXEL_FORMAT = 10U,
  EMUXR_RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO = 32U,
  EMUXR_RETRO_ENVIRONMENT_EXPERIMENTAL = 0x10000U,
  EMUXR_RETRO_ENVIRONMENT_GET_INPUT_BITMASKS =
      EMUXR_RETRO_ENVIRONMENT_EXPERIMENTAL | 51U,
  EMUXR_RETRO_DEVICE_JOYPAD = 1U,
  EMUXR_RETRO_DEVICE_ID_JOYPAD_B = 0U,
  EMUXR_RETRO_DEVICE_ID_JOYPAD_R3 = 15U,
  EMUXR_RETRO_DEVICE_ID_JOYPAD_MASK = 256U,
  EMUXR_LIBRETRO_MAX_PORTS = 4U
};

typedef enum emuxr_libretro_pixel_format {
  EMUXR_RETRO_PIXEL_FORMAT_0RGB1555 = 0,
  EMUXR_RETRO_PIXEL_FORMAT_XRGB8888 = 1,
  EMUXR_RETRO_PIXEL_FORMAT_RGB565 = 2
} emuxr_libretro_pixel_format;

/* RGB565 is normally expanded to full 8-bit channels. mGBA's COLOR_5_6_5
 * libretro surface is standard RGB565 (not BGR), but it originates from GBA
 * 5:5:5 pixels. This opt-in profile restores the source 5-bit green value
 * before 8-bit expansion, matching direct mGBA byte-for-byte. */
typedef enum emuxr_libretro_rgb565_conversion {
  EMUXR_LIBRETRO_RGB565_STANDARD = 0,
  EMUXR_LIBRETRO_RGB565_MGBA_GBA555 = 1
} emuxr_libretro_rgb565_conversion;

typedef struct emuxr_libretro_game_info {
  const char *path;
  const void *data;
  size_t size;
  const char *meta;
} emuxr_libretro_game_info;

typedef struct emuxr_libretro_system_av_info {
  uint32_t base_width;
  uint32_t base_height;
  uint32_t max_width;
  uint32_t max_height;
  float aspect_ratio;
  double frames_per_second;
  double audio_sample_rate;
} emuxr_libretro_system_av_info;

typedef bool (*emuxr_libretro_environment_t)(unsigned command, void *data);
typedef void (*emuxr_libretro_video_refresh_t)(const void *data,
                                                unsigned width,
                                                unsigned height,
                                                size_t pitch);
typedef void (*emuxr_libretro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (*emuxr_libretro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (*emuxr_libretro_input_poll_t)(void);
typedef int16_t (*emuxr_libretro_input_state_t)(unsigned port,
                                                 unsigned device,
                                                 unsigned index,
                                                 unsigned id);

/* The function table is supplied by a statically linked or dynamically
 * resolved libretro core. All calls through this table happen on the one
 * emulation worker; this host is deliberately not a cross-thread core proxy. */
typedef struct emuxr_libretro_api {
  void (*set_environment)(emuxr_libretro_environment_t callback);
  void (*set_video_refresh)(emuxr_libretro_video_refresh_t callback);
  void (*set_audio_sample)(emuxr_libretro_audio_sample_t callback);
  void (*set_audio_sample_batch)(emuxr_libretro_audio_sample_batch_t callback);
  void (*set_input_poll)(emuxr_libretro_input_poll_t callback);
  void (*set_input_state)(emuxr_libretro_input_state_t callback);
  void (*init)(void);
  void (*deinit)(void);
  bool (*load_game)(const emuxr_libretro_game_info *game);
  void (*unload_game)(void);
  void (*run)(void);
  void (*get_system_av_info)(emuxr_libretro_system_av_info *out_info);
  size_t (*serialize_size)(void);
  bool (*serialize)(void *data, size_t size);
  bool (*unserialize)(const void *data, size_t size);
  /* Core-declared RGB565 conversion only; zero keeps generic libretro RGB565
   * behavior. Do not infer this from RETRO_PIXEL_FORMAT_RGB565 alone. */
  emuxr_libretro_rgb565_conversion rgb565_conversion;
} emuxr_libretro_api;

/* Pixel memory belongs to the host and remains valid only for the duration of
 * the frame sink callback. A bridge sink must copy it into an owned lease. */
typedef struct emuxr_rgba_frame {
  const uint8_t *rgba8;
  uint32_t width;
  uint32_t height;
  uint32_t row_stride_bytes;
} emuxr_rgba_frame;

typedef int (*emuxr_libretro_video_sink)(void *opaque, const emuxr_rgba_frame *frame);
typedef uint32_t (*emuxr_libretro_audio_sink)(void *opaque,
                                               const int16_t *stereo_interleaved,
                                               uint32_t frames);
typedef bool (*emuxr_libretro_environment_fallback)(void *opaque,
                                                     unsigned command,
                                                     void *data);

typedef struct emuxr_libretro_host_config {
  emuxr_libretro_video_sink video_sink;
  void *video_sink_opaque;
  emuxr_libretro_audio_sink audio_sink;
  void *audio_sink_opaque;
  emuxr_libretro_environment_fallback environment_fallback;
  void *environment_fallback_opaque;
} emuxr_libretro_host_config;

typedef struct emuxr_libretro_host emuxr_libretro_host;

emuxr_libretro_host *emuxr_libretro_host_create(const emuxr_libretro_api *api,
                                                 const emuxr_libretro_host_config *config);
void emuxr_libretro_host_destroy(emuxr_libretro_host *host);

int emuxr_libretro_host_load_game(emuxr_libretro_host *host,
                                  const emuxr_libretro_game_info *game);
void emuxr_libretro_host_unload_game(emuxr_libretro_host *host);
int emuxr_libretro_host_run_frame(emuxr_libretro_host *host);
int emuxr_libretro_host_get_system_av_info(emuxr_libretro_host *host,
                                           emuxr_libretro_system_av_info *out_info);

/* Thread-safe input mailbox: a set bit maps directly to its libretro joypad
 * identifier. The emulation worker can read individual buttons or the
 * standard RETRO_DEVICE_ID_JOYPAD_MASK aggregate through retro_input_state(). */
void emuxr_libretro_host_set_joypad_mask(emuxr_libretro_host *host,
                                         uint32_t port,
                                         uint16_t mask);

size_t emuxr_libretro_host_state_size(const emuxr_libretro_host *host);
int emuxr_libretro_host_save_state(emuxr_libretro_host *host, void *data, size_t size);
int emuxr_libretro_host_load_state(emuxr_libretro_host *host,
                                   const void *data,
                                   size_t size);

emuxr_libretro_pixel_format emuxr_libretro_host_pixel_format(const emuxr_libretro_host *host);
uint64_t emuxr_libretro_host_video_frame_count(const emuxr_libretro_host *host);
uint64_t emuxr_libretro_host_rejected_video_frame_count(const emuxr_libretro_host *host);

#ifdef __cplusplus
}
#endif

#endif
