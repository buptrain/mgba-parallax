#include "gba_xr_mgba_audio_defaults.h"

#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)

int main(void) {
  CHECK(GBAXR_MGBA_AUDIO_DEFAULT_VOLUME == 0x100);
  CHECK(GBAXR_MGBA_AUDIO_DEFAULT_MUTE == 0);
  CHECK(GBAXR_MGBA_AUDIO_DEFAULT_SYNC == 0);
  return 0;
}
