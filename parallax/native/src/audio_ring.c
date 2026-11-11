#include "gba_xr_audio_ring.h"
#include <stdatomic.h>
#include <stdlib.h>

/* Single producer / single consumer ring. The producer never advances read:
 * when Unity is late, new frames are dropped instead of racing the consumer
 * or playing stale audio with growing latency. */
struct gbaxr_audio_ring {
  int16_t *samples;
  uint32_t capacity;
  atomic_uint read;
  atomic_uint write;
  atomic_ullong dropped_frames;
};
gbaxr_audio_ring *gbaxr_audio_ring_create(uint32_t capacity_frames) {
  gbaxr_audio_ring *r = capacity_frames ? calloc(1, sizeof(*r)) : NULL;
  if (!r) return NULL; r->samples = calloc((size_t)capacity_frames * 2U, sizeof(*r->samples));
  if (!r->samples) { free(r); return NULL; }
  r->capacity = capacity_frames;
  atomic_init(&r->read, 0U);
  atomic_init(&r->write, 0U);
  atomic_init(&r->dropped_frames, 0U);
  return r;
}
void gbaxr_audio_ring_destroy(gbaxr_audio_ring *r) { if (r) { free(r->samples); free(r); } }
uint32_t gbaxr_audio_ring_available(const gbaxr_audio_ring *r) {
  uint32_t w, q;
  if (!r) return 0U;
  w = atomic_load_explicit(&r->write, memory_order_acquire);
  q = atomic_load_explicit(&r->read, memory_order_acquire);
  return w - q;
}
uint64_t gbaxr_audio_ring_dropped_frames(const gbaxr_audio_ring *r) {
  return r ? atomic_load_explicit(&r->dropped_frames, memory_order_relaxed) : 0U;
}
uint32_t gbaxr_audio_ring_write(gbaxr_audio_ring *r, const int16_t *s, uint32_t n) {
  uint32_t i, w;
  if (!r || !s) return 0U;
  w = atomic_load_explicit(&r->write, memory_order_relaxed);
  for (i = 0U; i < n; ++i) {
    uint32_t q = atomic_load_explicit(&r->read, memory_order_acquire);
    if (w - q >= r->capacity) {
      atomic_fetch_add_explicit(&r->dropped_frames, (uint64_t)(n - i), memory_order_relaxed);
      break;
    }
    r->samples[(w % r->capacity) * 2U] = s[i * 2U];
    r->samples[(w % r->capacity) * 2U + 1U] = s[i * 2U + 1U];
    ++w;
    atomic_store_explicit(&r->write, w, memory_order_release);
  }
  return i;
}
uint32_t gbaxr_audio_ring_read(gbaxr_audio_ring *r, int16_t *s, uint32_t n) {
  uint32_t i, q;
  if (!r || !s) return 0U;
  q = atomic_load_explicit(&r->read, memory_order_relaxed);
  for (i = 0U; i < n; ++i) {
    uint32_t w = atomic_load_explicit(&r->write, memory_order_acquire);
    if (q == w) break;
    s[i * 2U] = r->samples[(q % r->capacity) * 2U];
    s[i * 2U + 1U] = r->samples[(q % r->capacity) * 2U + 1U];
    ++q;
    atomic_store_explicit(&r->read, q, memory_order_release);
  }
  return i;
}
