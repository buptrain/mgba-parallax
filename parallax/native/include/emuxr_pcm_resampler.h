#ifndef EMUXR_PCM_RESAMPLER_H
#define EMUXR_PCM_RESAMPLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A small streaming stereo S16 linear resampler. It owns no buffers and makes
 * no allocation, so a worker can feed a bounded audio ring directly from a
 * libretro batch callback. The sink is allowed to reject frames when its ring
 * is full; source input is still consumed exactly once. */
typedef uint32_t (*emuxr_pcm_resample_sink)(void *opaque,
                                            const int16_t *stereo,
                                            uint32_t frames);

typedef struct emuxr_pcm_resampler {
  uint32_t source_rate;
  uint32_t target_rate;
  uint64_t source_frame_index;
  uint64_t next_output_time;
  int16_t previous_left;
  int16_t previous_right;
  int has_previous;
} emuxr_pcm_resampler;

int emuxr_pcm_resampler_init(emuxr_pcm_resampler *resampler,
                              uint32_t source_rate,
                              uint32_t target_rate);
void emuxr_pcm_resampler_reset(emuxr_pcm_resampler *resampler);
uint64_t emuxr_pcm_resampler_process(emuxr_pcm_resampler *resampler,
                                     const int16_t *stereo,
                                     uint32_t frames,
                                     emuxr_pcm_resample_sink sink,
                                     void *sink_opaque);

#ifdef __cplusplus
}
#endif

#endif
