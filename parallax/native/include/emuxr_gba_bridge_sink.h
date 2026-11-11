#ifndef EMUXR_GBA_BRIDGE_SINK_H
#define EMUXR_GBA_BRIDGE_SINK_H

#include "emuxr_libretro_host.h"
#include "gba_xr_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Temporary ABI-v3 adapter. It is deliberately the only libretro-host code
 * that knows the current bridge's fixed GBA geometry. The generic host stays
 * reusable for later dynamic-size frame leases. */
int emuxr_gba_bridge_sink_publish(void *opaque, const emuxr_rgba_frame *frame);

#ifdef __cplusplus
}
#endif

#endif
