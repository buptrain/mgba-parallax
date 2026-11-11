#include "emuxr_libretro_host.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct emuxr_libretro_host {
  emuxr_libretro_api api;
  emuxr_libretro_host_config config;
  atomic_uint_fast16_t joypad_masks[EMUXR_LIBRETRO_MAX_PORTS];
  emuxr_libretro_pixel_format pixel_format;
  emuxr_libretro_rgb565_conversion rgb565_conversion;
  uint8_t *rgba_scratch;
  size_t rgba_scratch_bytes;
  int initialized;
  int game_loaded;
  uint64_t video_frame_count;
  uint64_t rejected_video_frame_count;
};

/* libretro callbacks carry no user pointer, so an in-process frontend may own
 * only one active standard core. The product-level worker enforces this by
 * keeping one active core session. */
static emuxr_libretro_host *active_host;

static uint8_t expand_5(uint16_t value) {
  return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t expand_6(uint16_t value) {
  return (uint8_t)((value << 2U) | (value >> 4U));
}

static int checked_frame_bytes(unsigned width, unsigned height, size_t *out_bytes) {
  size_t pixels;
  if (out_bytes == NULL || width == 0U || height == 0U ||
      (size_t)width > SIZE_MAX / (size_t)height) {
    return 0;
  }
  pixels = (size_t)width * (size_t)height;
  if (pixels > SIZE_MAX / 4U) {
    return 0;
  }
  *out_bytes = pixels * 4U;
  return 1;
}

static int ensure_rgba_scratch(emuxr_libretro_host *host, size_t bytes) {
  uint8_t *replacement;
  if (bytes <= host->rgba_scratch_bytes) {
    return 1;
  }
  replacement = (uint8_t *)realloc(host->rgba_scratch, bytes);
  if (replacement == NULL) {
    return 0;
  }
  host->rgba_scratch = replacement;
  host->rgba_scratch_bytes = bytes;
  return 1;
}

static int convert_video_to_rgba(emuxr_libretro_host *host,
                                 const void *data,
                                 unsigned width,
                                 unsigned height,
                                 size_t pitch) {
  uint32_t y;
  size_t required_bytes;
  size_t minimum_pitch;
  const uint8_t *source = (const uint8_t *)data;
  if (!checked_frame_bytes(width, height, &required_bytes)) {
    return 0;
  }
  if (width > UINT32_MAX / 4U) {
    return 0;
  }
  if (host->pixel_format == EMUXR_RETRO_PIXEL_FORMAT_XRGB8888) {
    minimum_pitch = (size_t)width * 4U;
  } else {
    minimum_pitch = (size_t)width * 2U;
  }
  if (pitch < minimum_pitch || !ensure_rgba_scratch(host, required_bytes)) {
    return 0;
  }
  for (y = 0U; y < height; ++y) {
    uint32_t x;
    const uint8_t *input_row = source + (size_t)y * pitch;
    uint8_t *output_row = host->rgba_scratch + (size_t)y * (size_t)width * 4U;
    for (x = 0U; x < width; ++x) {
      uint8_t *output = output_row + (size_t)x * 4U;
      if (host->pixel_format == EMUXR_RETRO_PIXEL_FORMAT_XRGB8888) {
        uint32_t pixel;
        memcpy(&pixel, input_row + (size_t)x * 4U, sizeof(pixel));
        output[0] = (uint8_t)(pixel >> 16U);
        output[1] = (uint8_t)(pixel >> 8U);
        output[2] = (uint8_t)pixel;
      } else {
        uint16_t pixel;
        memcpy(&pixel, input_row + (size_t)x * 2U, sizeof(pixel));
        if (host->pixel_format == EMUXR_RETRO_PIXEL_FORMAT_RGB565) {
          if (host->rgb565_conversion == EMUXR_LIBRETRO_RGB565_MGBA_GBA555) {
            /* mGBA has already converted GBA BGR555 to RGB565. Its sixth
             * green bit is a duplicate of the original five-bit green MSB;
             * restore that five-bit source before expansion. R/B are normal
             * RGB565. Swapping R/B here would be incorrect and makes cyan
             * yellow. */
            output[0] = expand_5((uint16_t)(pixel >> 11U));
            output[1] = expand_5((uint16_t)((pixel >> 6U) & 0x1FU));
            output[2] = expand_5((uint16_t)(pixel & 0x1FU));
          } else {
            output[0] = expand_5((uint16_t)(pixel >> 11U));
            output[1] = expand_6((uint16_t)((pixel >> 5U) & 0x3FU));
            output[2] = expand_5((uint16_t)(pixel & 0x1FU));
          }
        } else {
          output[0] = expand_5((uint16_t)(pixel >> 10U));
          output[1] = expand_5((uint16_t)((pixel >> 5U) & 0x1FU));
          output[2] = expand_5((uint16_t)(pixel & 0x1FU));
        }
      }
      output[3] = UINT8_MAX;
    }
  }
  return 1;
}

static bool frontend_environment(unsigned command, void *data) {
  emuxr_libretro_host *host = active_host;
  if (host == NULL) {
    return false;
  }
  if (command == EMUXR_RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
    const emuxr_libretro_pixel_format format = data == NULL
        ? (emuxr_libretro_pixel_format)-1
        : *(const emuxr_libretro_pixel_format *)data;
    if (format < EMUXR_RETRO_PIXEL_FORMAT_0RGB1555 ||
        format > EMUXR_RETRO_PIXEL_FORMAT_RGB565) {
      return false;
    }
    host->pixel_format = format;
    return true;
  }
  if (command == EMUXR_RETRO_ENVIRONMENT_GET_CAN_DUPE) {
    if (data == NULL) {
      return false;
    }
    *(bool *)data = true;
    return true;
  }
  if (command == EMUXR_RETRO_ENVIRONMENT_GET_INPUT_BITMASKS) {
    return true;
  }
  return host->config.environment_fallback != NULL &&
      host->config.environment_fallback(host->config.environment_fallback_opaque, command, data);
}

static void frontend_video_refresh(const void *data,
                                   unsigned width,
                                   unsigned height,
                                   size_t pitch) {
  emuxr_libretro_host *host = active_host;
  emuxr_rgba_frame frame;
  if (host == NULL || data == NULL) {
    return;
  }
  /* RETRO_HW_FRAME_BUFFER_VALID is an all-bits-set pointer. Hardware-frame
   * sharing is intentionally unsupported until the Vulkan interop contract is
   * designed; resolved CPU video remains the safe baseline. */
  if ((uintptr_t)data == UINTPTR_MAX || !convert_video_to_rgba(host, data, width, height, pitch)) {
    ++host->rejected_video_frame_count;
    return;
  }
  frame.rgba8 = host->rgba_scratch;
  frame.width = width;
  frame.height = height;
  frame.row_stride_bytes = width * 4U;
  if (host->config.video_sink == NULL ||
      host->config.video_sink(host->config.video_sink_opaque, &frame) != 0) {
    ++host->rejected_video_frame_count;
    return;
  }
  ++host->video_frame_count;
}

static size_t frontend_audio_sample_batch(const int16_t *data, size_t frames) {
  emuxr_libretro_host *host = active_host;
  uint32_t requested;
  if (host == NULL || data == NULL) {
    return 0U;
  }
  requested = frames > UINT32_MAX ? UINT32_MAX : (uint32_t)frames;
  if (host->config.audio_sink == NULL) {
    return requested;
  }
  return host->config.audio_sink(host->config.audio_sink_opaque, data, requested);
}

static void frontend_audio_sample(int16_t left, int16_t right) {
  const int16_t frame[2] = { left, right };
  (void)frontend_audio_sample_batch(frame, 1U);
}

static void frontend_input_poll(void) {
}

static int16_t frontend_input_state(unsigned port,
                                    unsigned device,
                                    unsigned index,
                                    unsigned id) {
  emuxr_libretro_host *host = active_host;
  uint16_t mask;
  (void)index;
  if (host == NULL || device != EMUXR_RETRO_DEVICE_JOYPAD ||
      port >= EMUXR_LIBRETRO_MAX_PORTS) {
    return 0;
  }
  mask = (uint16_t)atomic_load_explicit(&host->joypad_masks[port], memory_order_acquire);
  if (id == EMUXR_RETRO_DEVICE_ID_JOYPAD_MASK) {
    return (int16_t)mask;
  }
  if (id > EMUXR_RETRO_DEVICE_ID_JOYPAD_R3) {
    return 0;
  }
  return (mask & ((uint16_t)1U << id)) != 0U ? 1 : 0;
}

static int api_is_complete(const emuxr_libretro_api *api) {
  return api != NULL && api->set_environment != NULL && api->set_video_refresh != NULL &&
      api->set_audio_sample != NULL && api->set_audio_sample_batch != NULL &&
      api->set_input_poll != NULL && api->set_input_state != NULL && api->init != NULL &&
      api->deinit != NULL && api->load_game != NULL && api->unload_game != NULL &&
      api->run != NULL && api->get_system_av_info != NULL && api->serialize_size != NULL && api->serialize != NULL &&
      api->unserialize != NULL;
}

emuxr_libretro_host *emuxr_libretro_host_create(const emuxr_libretro_api *api,
                                                 const emuxr_libretro_host_config *config) {
  emuxr_libretro_host *host;
  uint32_t port;
  if (!api_is_complete(api) || config == NULL || config->video_sink == NULL || active_host != NULL) {
    return NULL;
  }
  host = (emuxr_libretro_host *)calloc(1U, sizeof(*host));
  if (host == NULL) {
    return NULL;
  }
  host->api = *api;
  host->config = *config;
  host->pixel_format = EMUXR_RETRO_PIXEL_FORMAT_0RGB1555;
  host->rgb565_conversion = api->rgb565_conversion;
  for (port = 0U; port < EMUXR_LIBRETRO_MAX_PORTS; ++port) {
    atomic_init(&host->joypad_masks[port], 0U);
  }
  active_host = host;
  host->api.set_environment(frontend_environment);
  host->api.set_video_refresh(frontend_video_refresh);
  host->api.set_audio_sample(frontend_audio_sample);
  host->api.set_audio_sample_batch(frontend_audio_sample_batch);
  host->api.set_input_poll(frontend_input_poll);
  host->api.set_input_state(frontend_input_state);
  host->api.init();
  host->initialized = 1;
  return host;
}

void emuxr_libretro_host_unload_game(emuxr_libretro_host *host) {
  if (host != NULL && host->game_loaded) {
    host->api.unload_game();
    host->game_loaded = 0;
  }
}

void emuxr_libretro_host_destroy(emuxr_libretro_host *host) {
  if (host == NULL) {
    return;
  }
  emuxr_libretro_host_unload_game(host);
  if (host->initialized) {
    host->api.deinit();
  }
  if (active_host == host) {
    active_host = NULL;
  }
  free(host->rgba_scratch);
  free(host);
}

int emuxr_libretro_host_load_game(emuxr_libretro_host *host,
                                  const emuxr_libretro_game_info *game) {
  if (host == NULL || game == NULL || (game->path == NULL && game->data == NULL)) {
    return -1;
  }
  emuxr_libretro_host_unload_game(host);
  if (!host->api.load_game(game)) {
    return -1;
  }
  host->game_loaded = 1;
  return 0;
}

int emuxr_libretro_host_run_frame(emuxr_libretro_host *host) {
  if (host == NULL || !host->game_loaded) {
    return -1;
  }
  host->api.run();
  return 0;
}

int emuxr_libretro_host_get_system_av_info(emuxr_libretro_host *host,
                                           emuxr_libretro_system_av_info *out_info) {
  if (host == NULL || !host->game_loaded || out_info == NULL) {
    return -1;
  }
  memset(out_info, 0, sizeof(*out_info));
  host->api.get_system_av_info(out_info);
  return out_info->base_width == 0U || out_info->base_height == 0U ||
      out_info->frames_per_second <= 0.0 || out_info->audio_sample_rate <= 0.0 ? -1 : 0;
}

void emuxr_libretro_host_set_joypad_mask(emuxr_libretro_host *host,
                                         uint32_t port,
                                         uint16_t mask) {
  if (host != NULL && port < EMUXR_LIBRETRO_MAX_PORTS) {
    atomic_store_explicit(&host->joypad_masks[port], mask, memory_order_release);
  }
}

size_t emuxr_libretro_host_state_size(const emuxr_libretro_host *host) {
  return host == NULL || !host->game_loaded ? 0U : host->api.serialize_size();
}

int emuxr_libretro_host_save_state(emuxr_libretro_host *host, void *data, size_t size) {
  const size_t expected = emuxr_libretro_host_state_size(host);
  return expected == 0U || data == NULL || size != expected || !host->api.serialize(data, size) ? -1 : 0;
}

int emuxr_libretro_host_load_state(emuxr_libretro_host *host,
                                   const void *data,
                                   size_t size) {
  const size_t expected = emuxr_libretro_host_state_size(host);
  return expected == 0U || data == NULL || size != expected || !host->api.unserialize(data, size) ? -1 : 0;
}

emuxr_libretro_pixel_format emuxr_libretro_host_pixel_format(const emuxr_libretro_host *host) {
  return host == NULL ? EMUXR_RETRO_PIXEL_FORMAT_0RGB1555 : host->pixel_format;
}

uint64_t emuxr_libretro_host_video_frame_count(const emuxr_libretro_host *host) {
  return host == NULL ? 0U : host->video_frame_count;
}

uint64_t emuxr_libretro_host_rejected_video_frame_count(const emuxr_libretro_host *host) {
  return host == NULL ? 0U : host->rejected_video_frame_count;
}
