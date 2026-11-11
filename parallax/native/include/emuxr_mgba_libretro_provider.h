#ifndef EMUXR_MGBA_LIBRETRO_PROVIDER_H
#define EMUXR_MGBA_LIBRETRO_PROVIDER_H

#include "emuxr_libretro_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the standard libretro entry points exported by the pinned mGBA
 * libretro core. The provider contains no PPU-layer extraction: it is the
 * resolved-frame baseline used to prove host parity before the existing mGBA
 * provenance adapter is moved behind an optional composition extension. */
const emuxr_libretro_api *emuxr_mgba_libretro_api(void);

#ifdef __cplusplus
}
#endif

#endif
