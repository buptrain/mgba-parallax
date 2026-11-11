#include "gba_xr_audio_ring.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)
int main(void) {
  int16_t in[] = {1,2,3,4,5,6,7,8,9,10}; int16_t out[10] = {0};
  gbaxr_audio_ring *r = gbaxr_audio_ring_create(4);
  CHECK(r); CHECK(gbaxr_audio_ring_write(r, in, 3) == 3); CHECK(gbaxr_audio_ring_read(r, out, 2) == 2);
  CHECK(out[0] == 1 && out[3] == 4); CHECK(gbaxr_audio_ring_read(r, out, 2) == 1);
  CHECK(gbaxr_audio_ring_write(r, in, 5) == 4); CHECK(gbaxr_audio_ring_available(r) == 4);
  CHECK(gbaxr_audio_ring_dropped_frames(r) == 1U);
  CHECK(gbaxr_audio_ring_read(r, out, 4) == 4); CHECK(out[0] == 1 && out[7] == 8);
  CHECK(gbaxr_audio_ring_read(r, out, 1) == 0);
  gbaxr_audio_ring_destroy(r); puts("audio ring tests passed"); return 0;
}
