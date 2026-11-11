#include "emuxr_pcm_resampler.h"

#include <limits.h>
#include <string.h>

static int16_t interpolate_sample(int16_t previous,
                                  int16_t current,
                                  uint64_t numerator,
                                  uint32_t denominator) {
  const int64_t delta = (int64_t)current - previous;
  const int64_t adjustment = delta >= 0 ? (int64_t)denominator / 2 : -((int64_t)denominator / 2);
  int64_t value = (int64_t)previous + (delta * (int64_t)numerator + adjustment) /
      (int64_t)denominator;
  if (value > INT16_MAX) value = INT16_MAX;
  if (value < INT16_MIN) value = INT16_MIN;
  return (int16_t)value;
}

static void publish_frame(emuxr_pcm_resample_sink sink,
                          void *sink_opaque,
                          int16_t left,
                          int16_t right) {
  const int16_t frame[2] = { left, right };
  (void)sink(sink_opaque, frame, 1U);
}

int emuxr_pcm_resampler_init(emuxr_pcm_resampler *resampler,
                              uint32_t source_rate,
                              uint32_t target_rate) {
  if (resampler == NULL || source_rate == 0U || target_rate == 0U) {
    return -1;
  }
  memset(resampler, 0, sizeof(*resampler));
  resampler->source_rate = source_rate;
  resampler->target_rate = target_rate;
  return 0;
}

void emuxr_pcm_resampler_reset(emuxr_pcm_resampler *resampler) {
  if (resampler != NULL) {
    resampler->source_frame_index = 0U;
    resampler->next_output_time = 0U;
    resampler->previous_left = 0;
    resampler->previous_right = 0;
    resampler->has_previous = 0;
  }
}

uint64_t emuxr_pcm_resampler_process(emuxr_pcm_resampler *resampler,
                                     const int16_t *stereo,
                                     uint32_t frames,
                                     emuxr_pcm_resample_sink sink,
                                     void *sink_opaque) {
  uint32_t frame;
  uint64_t emitted = 0U;
  if (resampler == NULL || stereo == NULL || sink == NULL ||
      resampler->source_rate == 0U || resampler->target_rate == 0U) {
    return 0U;
  }
  for (frame = 0U; frame < frames; ++frame) {
    const int16_t current_left = stereo[frame * 2U];
    const int16_t current_right = stereo[frame * 2U + 1U];
    if (!resampler->has_previous) {
      resampler->previous_left = current_left;
      resampler->previous_right = current_right;
      resampler->has_previous = 1;
      publish_frame(sink, sink_opaque, current_left, current_right);
      ++emitted;
      resampler->next_output_time = resampler->source_rate;
      continue;
    }
    ++resampler->source_frame_index;
    while (resampler->next_output_time <=
        resampler->source_frame_index * (uint64_t)resampler->target_rate) {
      const uint64_t interval_start = (resampler->source_frame_index - 1U) *
          (uint64_t)resampler->target_rate;
      const uint64_t numerator = resampler->next_output_time - interval_start;
      publish_frame(sink, sink_opaque,
          interpolate_sample(resampler->previous_left, current_left, numerator,
                             resampler->target_rate),
          interpolate_sample(resampler->previous_right, current_right, numerator,
                             resampler->target_rate));
      ++emitted;
      resampler->next_output_time += resampler->source_rate;
    }
    resampler->previous_left = current_left;
    resampler->previous_right = current_right;
  }
  return emitted;
}
