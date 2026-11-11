#include "gba_xr_reference_compositor.h"

#include <stddef.h>

static int layer_is_before(const gbaxr_composite_layer *candidate,
                           const gbaxr_composite_layer *current) {
  if (candidate->priority != current->priority) {
    return candidate->priority < current->priority;
  }
  return candidate->stable_order < current->stable_order;
}

static uint8_t blend_channel(uint8_t foreground, uint8_t background, uint8_t alpha) {
  const uint32_t inverse = 255U - alpha;
  return (uint8_t)((((uint32_t)foreground * alpha) + ((uint32_t)background * inverse) + 127U) / 255U);
}

static void copy_pixel(uint8_t *dst, const uint8_t *src) {
  dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
}

static void alpha_over(uint8_t *dst, const uint8_t *src) {
  const uint8_t alpha = src[3];
  dst[0] = blend_channel(src[0], dst[0], alpha);
  dst[1] = blend_channel(src[1], dst[1], alpha);
  dst[2] = blend_channel(src[2], dst[2], alpha);
  dst[3] = (uint8_t)(alpha + (((uint32_t)dst[3] * (255U - alpha) + 127U) / 255U));
}

int gbaxr_reference_composite(const gbaxr_composite_layer *layers,
                              uint32_t layer_count,
                              uint32_t width,
                              uint32_t height,
                              uint8_t *out_rgba8) {
  uint32_t x;
  uint32_t y;
  if (layers == NULL || out_rgba8 == NULL || width == 0U || height == 0U) {
    return -1;
  }
  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      const gbaxr_composite_layer *selected;
      const gbaxr_composite_layer *cursor;
      const uint8_t *src;
      uint8_t *dst = out_rgba8 + ((size_t)y * width + x) * 4U;
      uint32_t index;
      selected = NULL;
      /* Validate all source planes and select the visible farthest layer. */
      for (index = 0; index < layer_count; ++index) {
        const gbaxr_composite_layer *candidate = &layers[index];
        if (candidate->image.rgba8 == NULL || candidate->image.width != width ||
            candidate->image.height != height || candidate->image.row_stride_bytes < width * 4U) {
          return -1;
        }
        src = candidate->image.rgba8 + (size_t)y * candidate->image.row_stride_bytes + x * 4U;
        if (src[3] != 0U && (selected == NULL || layer_is_before(selected, candidate))) {
          selected = candidate;
        }
      }
      if (selected == NULL) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0U;
        continue;
      }
      src = selected->image.rgba8 + (size_t)y * selected->image.row_stride_bytes + x * 4U;
      copy_pixel(dst, src);
      cursor = selected;

      /* Move toward the foreground. At each step take the closest visible plane. */
      for (;;) {
        selected = NULL;
        for (index = 0; index < layer_count; ++index) {
          const gbaxr_composite_layer *candidate = &layers[index];
          src = candidate->image.rgba8 + (size_t)y * candidate->image.row_stride_bytes + x * 4U;
          if (src[3] != 0U && layer_is_before(candidate, cursor) &&
              (selected == NULL || layer_is_before(selected, candidate))) {
            selected = candidate;
          }
        }
        if (selected == NULL) {
          break;
        }
        src = selected->image.rgba8 + (size_t)y * selected->image.row_stride_bytes + x * 4U;
        if (selected->blend_mode == GBAXR_REFERENCE_BLEND_ALPHA) {
          alpha_over(dst, src);
        } else {
          copy_pixel(dst, src);
        }
        cursor = selected;
      }
    }
  }
  return 0;
}
