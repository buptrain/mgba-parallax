#include "gba_xr_bridge.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <pthread.h>
#endif

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "failed: %s (%d)\n", #expr, __LINE__); return 1; } } while (0)

typedef struct thread_probe {
  gbaxr_context *context;
  gbaxr_image_view image;
  gbaxr_status acquire_status;
  gbaxr_status publish_status;
} thread_probe;

#if defined(_WIN32)
static DWORD WINAPI wrong_thread_attempt(LPVOID opaque) {
  thread_probe *probe = (thread_probe *)opaque;
  gbaxr_frame_lease lease;
  probe->acquire_status = gbaxr_acquire_latest_frame(probe->context, &lease);
  probe->publish_status = gbaxr_publish_resolved_rgba(probe->context, &probe->image);
  return 0U;
}
#else
static void *wrong_thread_attempt(void *opaque) {
  thread_probe *probe = (thread_probe *)opaque;
  gbaxr_frame_lease lease;
  probe->acquire_status = gbaxr_acquire_latest_frame(probe->context, &lease);
  probe->publish_status = gbaxr_publish_resolved_rgba(probe->context, &probe->image);
  return NULL;
}
#endif

/* An unbound context must adopt its producer from the native emulator worker,
 * while its consumer remains the Unity/render thread. */
#if defined(_WIN32)
static DWORD WINAPI publish_once(LPVOID opaque) {
  thread_probe *probe = (thread_probe *)opaque;
  probe->publish_status = gbaxr_publish_resolved_rgba(probe->context, &probe->image);
  return 0U;
}
#else
static void *publish_once(void *opaque) {
  thread_probe *probe = (thread_probe *)opaque;
  probe->publish_status = gbaxr_publish_resolved_rgba(probe->context, &probe->image);
  return NULL;
}
#endif

int main(void) {
  static uint8_t pixels[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4U];
  static gbaxr_pixel_provenance provenance[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];
  static uint8_t bg0_rgba[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4U];
  static uint8_t obj_rgba[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT * 4U];
  static uint8_t bg0_coverage[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];
  static uint8_t obj_coverage[GBAXR_GBA_WIDTH * GBAXR_GBA_HEIGHT];
  gbaxr_context *context = gbaxr_create(GBAXR_ABI_VERSION);
  gbaxr_image_view image = { pixels, GBAXR_GBA_WIDTH, GBAXR_GBA_HEIGHT, GBAXR_GBA_WIDTH * 4U };
  gbaxr_frame_lease lease;
  thread_probe probe;
  uint32_t frame;

  CHECK(context != NULL);
  CHECK(gbaxr_create(GBAXR_ABI_VERSION + 1U) == NULL);
  CHECK(gbaxr_step_frame(context) == GBAXR_STATE_ERROR);

  for (frame = 1U; frame <= 5U; ++frame) {
    memset(pixels, (int)frame, sizeof(pixels));
    CHECK(gbaxr_publish_resolved_rgba(context, &image) == GBAXR_OK);
  }
  CHECK(gbaxr_acquire_latest_frame(context, &lease) == GBAXR_OK);
  CHECK(lease.sequence == 5U);
  CHECK(lease.resolved_rgba.rgba8[0] == 5U);
  CHECK(gbaxr_acquire_latest_frame(context, &lease) == GBAXR_STATE_ERROR);
  CHECK(gbaxr_release_frame(context, &lease) == GBAXR_OK);
  CHECK(gbaxr_release_frame(context, &lease) == GBAXR_STATE_ERROR);

  probe.context = context;
  probe.image = image;
  probe.acquire_status = GBAXR_OK;
  probe.publish_status = GBAXR_OK;
#if defined(_WIN32)
  {
    HANDLE worker = CreateThread(NULL, 0U, wrong_thread_attempt, &probe, 0U, NULL);
    CHECK(worker != NULL);
    CHECK(WaitForSingleObject(worker, INFINITE) == WAIT_OBJECT_0);
    CHECK(CloseHandle(worker) != 0);
  }
#else
  {
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, wrong_thread_attempt, &probe) == 0);
    CHECK(pthread_join(worker, NULL) == 0);
  }
#endif
  CHECK(probe.acquire_status == GBAXR_WRONG_THREAD);
  CHECK(probe.publish_status == GBAXR_WRONG_THREAD);
  gbaxr_destroy(context);

  context = gbaxr_create_unbound(GBAXR_ABI_VERSION);
  CHECK(context != NULL);
  probe.context = context;
  probe.image = image;
  probe.publish_status = GBAXR_STATE_ERROR;
#if defined(_WIN32)
  {
    HANDLE worker = CreateThread(NULL, 0U, publish_once, &probe, 0U, NULL);
    CHECK(worker != NULL);
    CHECK(WaitForSingleObject(worker, INFINITE) == WAIT_OBJECT_0);
    CHECK(CloseHandle(worker) != 0);
  }
#else
  {
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, publish_once, &probe) == 0);
    CHECK(pthread_join(worker, NULL) == 0);
  }
#endif
  CHECK(probe.publish_status == GBAXR_OK);
  CHECK(gbaxr_acquire_latest_frame(context, &lease) == GBAXR_OK);
  CHECK(gbaxr_release_frame(context, &lease) == GBAXR_OK);
  gbaxr_destroy(context);

  context = gbaxr_create(GBAXR_ABI_VERSION);
  CHECK(context != NULL);
  provenance[0].source = GBAXR_SOURCE_BG2;
  provenance[0].priority = 1U;
  provenance[0].blend_effect = GBAXR_BLEND_ALPHA;
  provenance[0].window = 2U;
  CHECK(gbaxr_publish_frame(context, &image, provenance,
                            GBAXR_GBA_WIDTH * (uint32_t)sizeof(*provenance)) == GBAXR_OK);
  CHECK(gbaxr_acquire_latest_frame(context, &lease) == GBAXR_OK);
  CHECK(lease.provenance != NULL);
  CHECK(lease.provenance_stride_bytes == GBAXR_GBA_WIDTH * sizeof(*provenance));
  CHECK(((const gbaxr_pixel_provenance *)lease.provenance)[0].source == GBAXR_SOURCE_BG2);
  CHECK(((const gbaxr_pixel_provenance *)lease.provenance)[0].blend_effect == GBAXR_BLEND_ALPHA);
  CHECK(gbaxr_release_frame(context, &lease) == GBAXR_OK);
  gbaxr_destroy(context);

  context = gbaxr_create(GBAXR_ABI_VERSION);
  CHECK(context != NULL);
  memset(bg0_rgba, 0x11, sizeof(bg0_rgba));
  memset(obj_rgba, 0x22, sizeof(obj_rgba));
  memset(bg0_coverage, 0xFF, sizeof(bg0_coverage));
  memset(obj_coverage, 0x00, sizeof(obj_coverage));
  CHECK(gbaxr_publish_frame_layers(context, &image, NULL, 0U, NULL, 0U,
                                   GBAXR_LAYERS_VISIBLE_PARTITION) == GBAXR_INVALID_ARGUMENT);
  {
    gbaxr_layer_view layers[2] = {
      { GBAXR_SOURCE_BG0, { bg0_rgba, GBAXR_GBA_WIDTH, GBAXR_GBA_HEIGHT, GBAXR_GBA_WIDTH * 4U }, bg0_coverage, 2U },
      { GBAXR_SOURCE_OBJ, { obj_rgba, GBAXR_GBA_WIDTH, GBAXR_GBA_HEIGHT, GBAXR_GBA_WIDTH * 4U }, obj_coverage, 2U }
    };
    CHECK(gbaxr_publish_frame_layers(context, &image, NULL, 0U, layers, 2U,
                                     GBAXR_LAYERS_INDEPENDENT_CANDIDATES) == GBAXR_OK);
  }
  /* The bridge owns its copy; producer buffers may immediately be reused. */
  bg0_rgba[0] = 0xEE;
  obj_coverage[0] = 0xFF;
  CHECK(gbaxr_acquire_latest_frame(context, &lease) == GBAXR_OK);
  CHECK(lease.layers != NULL);
  CHECK(lease.layer_count == 2U);
  CHECK(lease.layer_semantics == GBAXR_LAYERS_INDEPENDENT_CANDIDATES);
  CHECK(lease.layers[0].source == GBAXR_SOURCE_BG0);
  CHECK(lease.layers[0].priority == 2U);
  CHECK(lease.layers[0].color.rgba8[0] == 0x11U);
  CHECK(lease.layers[0].coverage_mask[0] == 0xFFU);
  CHECK(lease.layers[1].source == GBAXR_SOURCE_OBJ);
  CHECK(lease.layers[1].color.rgba8[0] == 0x22U);
  CHECK(lease.layers[1].coverage_mask[0] == 0x00U);
  CHECK(gbaxr_release_frame(context, &lease) == GBAXR_OK);
  gbaxr_destroy(context);

  puts("bridge mailbox tests passed");
  return 0;
}
