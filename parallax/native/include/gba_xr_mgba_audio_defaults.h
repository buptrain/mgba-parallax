#ifndef GBA_XR_MGBA_AUDIO_DEFAULTS_H
#define GBA_XR_MGBA_AUDIO_DEFAULTS_H

/* mGBA's calloc-initialized mCoreOptions would otherwise map volume=0 into
 * GBAAudio.masterVolume. Keep frontend-owned defaults explicit and testable. */
enum {
  GBAXR_MGBA_AUDIO_DEFAULT_VOLUME = 0x100,
  GBAXR_MGBA_AUDIO_DEFAULT_MUTE = 0,
  /* The adapter owns pacing and drains audio on its worker thread. */
  GBAXR_MGBA_AUDIO_DEFAULT_SYNC = 0
};

#endif
