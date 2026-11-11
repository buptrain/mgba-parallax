#include "gba_xr_bridge.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
  typedef DWORD gbaxr_thread_id;
  static gbaxr_thread_id current_thread_id(void) { return GetCurrentThreadId(); }
  static int same_thread(gbaxr_thread_id left, gbaxr_thread_id right) { return left == right; }
#else
  #include <pthread.h>
  typedef pthread_t gbaxr_thread_id;
  static gbaxr_thread_id current_thread_id(void) { return pthread_self(); }
  static int same_thread(gbaxr_thread_id left, gbaxr_thread_id right) { return pthread_equal(left, right) != 0; }
#endif

enum {
  GBAXR_FRAME_BYTES = GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4,
  GBAXR_COVERAGE_BYTES = GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT,
  GBAXR_QUEUE_SIZE = 3
};

typedef enum gbaxr_slot_state {
  GBAXR_SLOT_FREE,
  GBAXR_SLOT_READY,
  GBAXR_SLOT_LEASED
} gbaxr_slot_state;

typedef struct gbaxr_slot {
  uint8_t rgba[GBAXR_FRAME_BYTES];
  gbaxr_pixel_provenance provenance[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];
  uint8_t layer_rgba[GBAXR_MAX_LAYERS][GBAXR_FRAME_BYTES];
  uint8_t layer_coverage[GBAXR_MAX_LAYERS][GBAXR_COVERAGE_BYTES];
  gbaxr_layer_view layers[GBAXR_MAX_LAYERS];
  uint32_t layer_count;
  gbaxr_layer_semantics layer_semantics;
  uint32_t provenance_stride_bytes;
  int has_provenance;
  uint64_t sequence;
  gbaxr_slot_state state;
} gbaxr_slot;

struct gbaxr_context {
  atomic_flag lock;
  gbaxr_thread_id producer_thread;
  int producer_bound;
  gbaxr_thread_id consumer_thread;
  int consumer_bound;
  uint64_t next_sequence;
  gbaxr_slot slots[GBAXR_QUEUE_SIZE];
};

static void lock_context(gbaxr_context *context) {
  while (atomic_flag_test_and_set_explicit(&context->lock, memory_order_acquire)) {
  }
}

static void unlock_context(gbaxr_context *context) {
  atomic_flag_clear_explicit(&context->lock, memory_order_release);
}

static gbaxr_slot *oldest_ready_slot(gbaxr_context *context) {
  gbaxr_slot *result = NULL;
  uint32_t index;
  for (index = 0U; index < GBAXR_QUEUE_SIZE; ++index) {
    gbaxr_slot *slot = &context->slots[index];
    if (slot->state == GBAXR_SLOT_READY &&
        (result == NULL || slot->sequence < result->sequence)) {
      result = slot;
    }
  }
  return result;
}

static gbaxr_slot *latest_ready_slot(gbaxr_context *context) {
  gbaxr_slot *result = NULL;
  uint32_t index;
  for (index = 0U; index < GBAXR_QUEUE_SIZE; ++index) {
    gbaxr_slot *slot = &context->slots[index];
    if (slot->state == GBAXR_SLOT_READY &&
        (result == NULL || slot->sequence > result->sequence)) {
      result = slot;
    }
  }
  return result;
}

static gbaxr_context *create_context(uint32_t requested_abi_version, int bind_producer) {
  gbaxr_context *context;
  if (requested_abi_version != GBAXR_ABI_VERSION) {
    return NULL;
  }
  context = (gbaxr_context *)calloc(1U, sizeof(*context));
  if (context != NULL) {
    atomic_flag_clear(&context->lock);
    context->producer_bound = bind_producer;
    if (bind_producer) {
      context->producer_thread = current_thread_id();
    }
  }
  return context;
}

gbaxr_context *gbaxr_create(uint32_t requested_abi_version) {
  return create_context(requested_abi_version, 1);
}

gbaxr_context *gbaxr_create_unbound(uint32_t requested_abi_version) {
  return create_context(requested_abi_version, 0);
}

void gbaxr_destroy(gbaxr_context *context) {
  free(context);
}

gbaxr_status gbaxr_step_frame(gbaxr_context *context) {
  if (context == NULL) {
    return GBAXR_INVALID_ARGUMENT;
  }
  if (!context->producer_bound || !same_thread(context->producer_thread, current_thread_id())) {
    return GBAXR_WRONG_THREAD;
  }
  /* The mGBA adapter supplies frame stepping and calls the publish hook at VBlank. */
  return GBAXR_STATE_ERROR;
}

gbaxr_status gbaxr_publish_resolved_rgba(gbaxr_context *context,
                                         const gbaxr_image_view *resolved_rgba) {
  return gbaxr_publish_frame(context, resolved_rgba, NULL, 0U);
}

gbaxr_status gbaxr_publish_frame(gbaxr_context *context,
                                 const gbaxr_image_view *resolved_rgba,
                                 const gbaxr_pixel_provenance *provenance,
                                 uint32_t provenance_stride_bytes) {
  return gbaxr_publish_frame_layers(context, resolved_rgba, provenance,
                                    provenance_stride_bytes, NULL, 0U, GBAXR_LAYERS_NONE);
}

gbaxr_status gbaxr_publish_frame_layers(gbaxr_context *context,
                                        const gbaxr_image_view *resolved_rgba,
                                        const gbaxr_pixel_provenance *provenance,
                                        uint32_t provenance_stride_bytes,
                                        const gbaxr_layer_view *layers,
                                        uint32_t layer_count,
                                        gbaxr_layer_semantics layer_semantics) {
  gbaxr_slot *slot = NULL;
  uint32_t index;
  uint32_t row;
  if (context == NULL || resolved_rgba == NULL || resolved_rgba->rgba8 == NULL ||
      resolved_rgba->width != GBAXR_GBA_WIDTH || resolved_rgba->height != GBAXR_GBA_HEIGHT ||
      resolved_rgba->row_stride_bytes < GBAXR_GBA_WIDTH * 4U) {
    return GBAXR_INVALID_ARGUMENT;
  }
  if (provenance != NULL && provenance_stride_bytes <
      GBAXR_GBA_WIDTH * (uint32_t)sizeof(*provenance)) {
    return GBAXR_INVALID_ARGUMENT;
  }
  if (layer_count > GBAXR_MAX_LAYERS || (layer_count != 0U && layers == NULL)) {
    return GBAXR_INVALID_ARGUMENT;
  }
  if ((layer_count == 0U && layer_semantics != GBAXR_LAYERS_NONE) ||
      (layer_count != 0U && layer_semantics != GBAXR_LAYERS_VISIBLE_PARTITION &&
       layer_semantics != GBAXR_LAYERS_INDEPENDENT_CANDIDATES)) {
    return GBAXR_INVALID_ARGUMENT;
  }
  for (index = 0U; index < layer_count; ++index) {
    uint32_t other;
    const gbaxr_layer_view *layer = &layers[index];
    if ((uint32_t)layer->source > (uint32_t)GBAXR_SOURCE_OBJ ||
        layer->color.rgba8 == NULL || layer->coverage_mask == NULL ||
        layer->color.width != GBAXR_GBA_WIDTH || layer->color.height != GBAXR_GBA_HEIGHT ||
        layer->color.row_stride_bytes < GBAXR_GBA_WIDTH * 4U) {
      return GBAXR_INVALID_ARGUMENT;
    }
    for (other = 0U; other < index; ++other) {
      if (layers[other].source == layer->source) {
        return GBAXR_INVALID_ARGUMENT;
      }
    }
  }
  lock_context(context);
  if (!context->producer_bound) {
    context->producer_thread = current_thread_id();
    context->producer_bound = 1;
  } else if (!same_thread(context->producer_thread, current_thread_id())) {
    unlock_context(context);
    return GBAXR_WRONG_THREAD;
  }
  for (index = 0U; index < GBAXR_QUEUE_SIZE; ++index) {
    if (context->slots[index].state == GBAXR_SLOT_FREE) {
      slot = &context->slots[index];
      break;
    }
  }
  if (slot == NULL) {
    slot = oldest_ready_slot(context);
  }
  if (slot == NULL) {
    unlock_context(context);
    return GBAXR_NO_FRAME;
  }
  for (row = 0U; row < GBAXR_GBA_HEIGHT; ++row) {
    memcpy(slot->rgba + (size_t)row * GBAXR_GBA_WIDTH * 4U,
           resolved_rgba->rgba8 + (size_t)row * resolved_rgba->row_stride_bytes,
           GBAXR_GBA_WIDTH * 4U);
    if (provenance != NULL) {
      memcpy(slot->provenance + (size_t)row * GBAXR_GBA_WIDTH,
             (const uint8_t *)provenance + (size_t)row * provenance_stride_bytes,
             GBAXR_GBA_WIDTH * sizeof(*provenance));
    }
  }
  slot->layer_count = layer_count;
  slot->layer_semantics = layer_semantics;
  for (index = 0U; index < layer_count; ++index) {
    const gbaxr_layer_view *layer = &layers[index];
    gbaxr_layer_view *slot_layer = &slot->layers[index];
    for (row = 0U; row < GBAXR_GBA_HEIGHT; ++row) {
      memcpy(slot->layer_rgba[index] + (size_t)row * GBAXR_GBA_WIDTH * 4U,
             layer->color.rgba8 + (size_t)row * layer->color.row_stride_bytes,
             GBAXR_GBA_WIDTH * 4U);
      memcpy(slot->layer_coverage[index] + (size_t)row * GBAXR_GBA_WIDTH,
             layer->coverage_mask + (size_t)row * GBAXR_GBA_WIDTH,
             GBAXR_GBA_WIDTH);
    }
    slot_layer->source = layer->source;
    slot_layer->color.rgba8 = slot->layer_rgba[index];
    slot_layer->color.width = GBAXR_GBA_WIDTH;
    slot_layer->color.height = GBAXR_GBA_HEIGHT;
    slot_layer->color.row_stride_bytes = GBAXR_GBA_WIDTH * 4U;
    slot_layer->coverage_mask = slot->layer_coverage[index];
    slot_layer->priority = layer->priority;
  }
  slot->has_provenance = provenance != NULL;
  slot->provenance_stride_bytes = provenance != NULL ?
      GBAXR_GBA_WIDTH * (uint32_t)sizeof(*provenance) : 0U;
  slot->sequence = ++context->next_sequence;
  slot->state = GBAXR_SLOT_READY;
  unlock_context(context);
  return GBAXR_OK;
}

gbaxr_status gbaxr_acquire_latest_frame(gbaxr_context *context,
                                        gbaxr_frame_lease *out_lease) {
  gbaxr_slot *slot;
  uint32_t index;
  if (context == NULL || out_lease == NULL) {
    return GBAXR_INVALID_ARGUMENT;
  }
  lock_context(context);
  if (!context->consumer_bound) {
    context->consumer_thread = current_thread_id();
    context->consumer_bound = 1;
  } else if (!same_thread(context->consumer_thread, current_thread_id())) {
    unlock_context(context);
    return GBAXR_WRONG_THREAD;
  }
  for (index = 0U; index < GBAXR_QUEUE_SIZE; ++index) {
    if (context->slots[index].state == GBAXR_SLOT_LEASED) {
      unlock_context(context);
      return GBAXR_STATE_ERROR;
    }
  }
  slot = latest_ready_slot(context);
  if (slot == NULL) {
    unlock_context(context);
    return GBAXR_NO_FRAME;
  }
  slot->state = GBAXR_SLOT_LEASED;
  out_lease->sequence = slot->sequence;
  out_lease->resolved_rgba.rgba8 = slot->rgba;
  out_lease->resolved_rgba.width = GBAXR_GBA_WIDTH;
  out_lease->resolved_rgba.height = GBAXR_GBA_HEIGHT;
  out_lease->resolved_rgba.row_stride_bytes = GBAXR_GBA_WIDTH * 4U;
  out_lease->layers = slot->layer_count != 0U ? slot->layers : NULL;
  out_lease->layer_count = slot->layer_count;
  out_lease->layer_semantics = slot->layer_semantics;
  out_lease->provenance = slot->has_provenance ? (const uint8_t *)slot->provenance : NULL;
  out_lease->provenance_stride_bytes = slot->provenance_stride_bytes;
  out_lease->blend_metadata = NULL;
  out_lease->blend_metadata_stride_bytes = 0U;
  unlock_context(context);
  return GBAXR_OK;
}

gbaxr_status gbaxr_release_frame(gbaxr_context *context,
                                 const gbaxr_frame_lease *lease) {
  uint32_t index;
  if (context == NULL || lease == NULL) {
    return GBAXR_INVALID_ARGUMENT;
  }
  lock_context(context);
  if (!context->consumer_bound || !same_thread(context->consumer_thread, current_thread_id())) {
    unlock_context(context);
    return GBAXR_WRONG_THREAD;
  }
  for (index = 0U; index < GBAXR_QUEUE_SIZE; ++index) {
    gbaxr_slot *slot = &context->slots[index];
    if (slot->state == GBAXR_SLOT_LEASED && slot->sequence == lease->sequence &&
        slot->rgba == lease->resolved_rgba.rgba8) {
      slot->state = GBAXR_SLOT_FREE;
      unlock_context(context);
      return GBAXR_OK;
    }
  }
  unlock_context(context);
  return GBAXR_STATE_ERROR;
}
