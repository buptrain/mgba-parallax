#include "emuxr_pcm_resampler.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; } } while (0)

typedef struct captured_audio {
  int16_t samples[16];
  uint32_t frames;
} captured_audio;

static uint32_t capture(void *opaque, const int16_t *stereo, uint32_t frames) {
  captured_audio *captured = (captured_audio *)opaque;
  if (captured == NULL || stereo == NULL || captured->frames + frames > 8U) return 0U;
  memcpy(captured->samples + captured->frames * 2U, stereo, frames * 2U * sizeof(*stereo));
  captured->frames += frames;
  return frames;
}

int main(void) {
  emuxr_pcm_resampler resampler;
  captured_audio captured;
  const int16_t upsampled[] = { 0, 0, 30000, -30000 };
  const int16_t same_rate[] = { 10, -10, 20, -20 };

  memset(&captured, 0, sizeof(captured));
  CHECK(emuxr_pcm_resampler_init(&resampler, 24000U, 48000U) == 0);
  CHECK(emuxr_pcm_resampler_process(&resampler, upsampled, 2U, capture, &captured) == 3U);
  CHECK(captured.frames == 3U);
  CHECK(captured.samples[0] == 0 && captured.samples[1] == 0);
  CHECK(captured.samples[2] == 15000 && captured.samples[3] == -15000);
  CHECK(captured.samples[4] == 30000 && captured.samples[5] == -30000);

  memset(&captured, 0, sizeof(captured));
  CHECK(emuxr_pcm_resampler_init(&resampler, 48000U, 48000U) == 0);
  CHECK(emuxr_pcm_resampler_process(&resampler, same_rate, 2U, capture, &captured) == 2U);
  CHECK(captured.frames == 2U);
  CHECK(captured.samples[0] == 10 && captured.samples[1] == -10);
  CHECK(captured.samples[2] == 20 && captured.samples[3] == -20);
  CHECK(emuxr_pcm_resampler_init(&resampler, 0U, 48000U) == -1);
  puts("PCM resampler tests passed");
  return 0;
}
