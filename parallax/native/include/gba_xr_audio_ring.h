#ifndef GBA_XR_AUDIO_RING_H
#define GBA_XR_AUDIO_RING_H

#include <stdint.h>

typedef struct gbaxr_audio_ring gbaxr_audio_ring;
gbaxr_audio_ring *gbaxr_audio_ring_create(uint32_t capacity_frames);
void gbaxr_audio_ring_destroy(gbaxr_audio_ring *ring);
uint32_t gbaxr_audio_ring_write(gbaxr_audio_ring *ring, const int16_t *stereo, uint32_t frames);
uint32_t gbaxr_audio_ring_read(gbaxr_audio_ring *ring, int16_t *stereo, uint32_t frames);
uint32_t gbaxr_audio_ring_available(const gbaxr_audio_ring *ring);
uint64_t gbaxr_audio_ring_dropped_frames(const gbaxr_audio_ring *ring);
#endif
