#include "emuxr_gba_bridge_sink.h"

int emuxr_gba_bridge_sink_publish(void *opaque, const emuxr_rgba_frame *frame) {
  gbaxr_context *bridge = (gbaxr_context *)opaque;
  gbaxr_image_view image;
  gbaxr_status status;
  if (bridge == NULL || frame == NULL || frame->rgba8 == NULL ||
      frame->width != GBAXR_GBA_WIDTH || frame->height != GBAXR_GBA_HEIGHT ||
      frame->row_stride_bytes < GBAXR_GBA_WIDTH * 4U) {
    return -1;
  }
  image.rgba8 = frame->rgba8;
  image.width = frame->width;
  image.height = frame->height;
  image.row_stride_bytes = frame->row_stride_bytes;
  status = gbaxr_publish_resolved_rgba(bridge, &image);
  /* The bridge is a latest-frame mailbox. A lease held by Unity may consume
   * every slot briefly; drop this presentation frame rather than poisoning the
   * emulator worker. Other failures are a contract violation. */
  return status == GBAXR_OK || status == GBAXR_NO_FRAME ? 0 : -1;
}
