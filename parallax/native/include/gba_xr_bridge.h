#ifndef GBA_XR_BRIDGE_H
#define GBA_XR_BRIDGE_H

#include <stdint.h>

#if defined(_WIN32)
  #if defined(GBAXR_BUILDING_DLL)
    #define GBAXR_API __declspec(dllexport)
  #elif defined(GBAXR_USING_DLL)
    #define GBAXR_API __declspec(dllimport)
  #else
    #define GBAXR_API
  #endif
#else
  #define GBAXR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
  GBAXR_ABI_VERSION = 3,
  GBAXR_GBA_WIDTH = 240,
  GBAXR_GBA_HEIGHT = 160,
  GBAXR_MAX_LAYERS = 6
};

typedef struct gbaxr_context gbaxr_context;

typedef enum gbaxr_status {
  GBAXR_OK = 0,
  GBAXR_NO_FRAME = 1,
  GBAXR_INVALID_ARGUMENT = -1,
  GBAXR_WRONG_THREAD = -2,
  GBAXR_STATE_ERROR = -3
} gbaxr_status;

typedef enum gbaxr_source_kind {
  GBAXR_SOURCE_BACKDROP = 0,
  GBAXR_SOURCE_BG0 = 1,
  GBAXR_SOURCE_BG1 = 2,
  GBAXR_SOURCE_BG2 = 3,
  GBAXR_SOURCE_BG3 = 4,
  GBAXR_SOURCE_OBJ = 5
} gbaxr_source_kind;

typedef enum gbaxr_blend_effect {
  GBAXR_BLEND_NONE = 0,
  GBAXR_BLEND_ALPHA = 1,
  GBAXR_BLEND_BRIGHTEN = 2,
  GBAXR_BLEND_DARKEN = 3
} gbaxr_blend_effect;

typedef enum gbaxr_layer_semantics {
  GBAXR_LAYERS_NONE = 0,
  /* Final visible pixels partitioned by provenance. Zero-parallax only. */
  GBAXR_LAYERS_VISIBLE_PARTITION = 1,
  /* Independent PPU candidates; eligible for a future depth policy. */
  GBAXR_LAYERS_INDEPENDENT_CANDIDATES = 2
} gbaxr_layer_semantics;

/* One final-composition diagnostic record per resolved GBA pixel. `window`
 * is a v1 bitfield: bit 0 is effective window-blend enable; bit 1 indicates
 * an OBJ-window pixel. It is diagnostic state, not a layer-coverage mask. */
typedef struct gbaxr_pixel_provenance {
  uint8_t source;
  uint8_t priority;
  uint8_t blend_effect;
  uint8_t window;
} gbaxr_pixel_provenance;

typedef struct gbaxr_blend_metadata {
  uint8_t effect;
  uint8_t eva;
  uint8_t evb;
  uint8_t flags;
} gbaxr_blend_metadata;

/* All pixel buffers are row-major, non-premultiplied RGBA8. */
typedef struct gbaxr_image_view {
  const uint8_t *rgba8;
  uint32_t width;
  uint32_t height;
  uint32_t row_stride_bytes;
} gbaxr_image_view;

typedef struct gbaxr_layer_view {
  gbaxr_source_kind source;
  gbaxr_image_view color;
  const uint8_t *coverage_mask; /* One byte per source pixel: 0 = transparent. */
  uint32_t priority;
} gbaxr_layer_view;

typedef struct gbaxr_frame_lease {
  uint64_t sequence;
  gbaxr_image_view resolved_rgba;
  const gbaxr_layer_view *layers;
  uint32_t layer_count;
  gbaxr_layer_semantics layer_semantics;
  const uint8_t *provenance; /* Optional, implementation-defined v1 diagnostic data. */
  uint32_t provenance_stride_bytes;
  const uint8_t *blend_metadata;
  uint32_t blend_metadata_stride_bytes;
} gbaxr_frame_lease;

/* Creation binds the producer thread. The opaque context owns core and queue state. */
GBAXR_API gbaxr_context *gbaxr_create(uint32_t requested_abi_version);
/* Creates a context whose producer is bound by its first publish call. Use
 * this when the emulator is created on a native worker thread. */
GBAXR_API gbaxr_context *gbaxr_create_unbound(uint32_t requested_abi_version);
GBAXR_API void gbaxr_destroy(gbaxr_context *context);

/* Producer-only. Advances emulation and publishes zero or more whole frames. */
GBAXR_API gbaxr_status gbaxr_step_frame(gbaxr_context *context);

/* Adapter hook, producer-only. Copies a complete 240x160 resolved frame into the
 * bounded presentation queue. It never retains caller-owned memory. */
GBAXR_API gbaxr_status gbaxr_publish_resolved_rgba(gbaxr_context *context,
                                                   const gbaxr_image_view *resolved_rgba);

/* Producer-only. Atomically publishes the resolved image and optional
 * per-pixel provenance. Provenance is copied into the lease slot; NULL means
 * unsupported for this frame and must never be interpreted as a layer. */
GBAXR_API gbaxr_status gbaxr_publish_frame(gbaxr_context *context,
                                            const gbaxr_image_view *resolved_rgba,
                                            const gbaxr_pixel_provenance *provenance,
                                            uint32_t provenance_stride_bytes);

/* Producer-only. Copies a resolved frame, optional provenance, and up to one
 * full-size coverage-backed plane per GBA source into the same lease slot.
 * The producer retains no pointers after this call. A zero layer count is the
 * resolved-frame fallback; nonzero layer publishes require unique sources and
 * a non-NULL coverage mask for every layer. */
GBAXR_API gbaxr_status gbaxr_publish_frame_layers(gbaxr_context *context,
                                                   const gbaxr_image_view *resolved_rgba,
                                                   const gbaxr_pixel_provenance *provenance,
                                                   uint32_t provenance_stride_bytes,
                                                   const gbaxr_layer_view *layers,
                                                   uint32_t layer_count,
                                                   gbaxr_layer_semantics layer_semantics);

/* The first acquire binds the consumer thread. Lease data stays valid until
 * gbaxr_release_frame. The consumer may hold at most one lease at a time. */
GBAXR_API gbaxr_status gbaxr_acquire_latest_frame(gbaxr_context *context,
                                                   gbaxr_frame_lease *out_lease);
GBAXR_API gbaxr_status gbaxr_release_frame(gbaxr_context *context,
                                           const gbaxr_frame_lease *lease);

#ifdef __cplusplus
}
#endif

#endif
