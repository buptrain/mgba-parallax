# Parallax instrumentation design

## Goal

Parallax Emu needs a trustworthy resolved GBA image before it can apply any
spatial presentation. This fork instruments mGBA's GBA software renderer to
publish final-pixel provenance alongside the resolved framebuffer. The native
adapter converts that data into copied frame leases behind a narrow C ABI.

The goal is **not** to turn every internal PPU buffer into a freely movable
3D plane. Pixel visibility can depend on GBA priority, OBJ ordering, windows,
alpha blending, brighten/darken effects, and affine backgrounds. A spatial
consumer must retain the resolved image as its visual oracle.

## Upstream renderer modifications

The reviewable source change is already applied to this fork and is also kept
as `parallax/patches/0001-final-pixel-provenance.patch`:

- `include/mgba/internal/gba/renderers/video-software.h` adds optional,
  caller-owned provenance and candidate-buffer hooks.
- `src/gba/renderers/software-private.h` preserves the final winner before
  renderer ordering flags are deliberately reused as composition sentinels.
- `src/gba/renderers/software-obj.c` records opaque OBJ candidate coverage.
- `src/gba/renderers/video-software.c` emits final provenance and Mode 0
  background candidate observations.

All hooks are optional. Without an attached buffer, ordinary upstream-style
software rendering continues without publishing Parallax metadata.

## Adapter boundary

`parallax/native/src/mgba_adapter.c` is the only direct adapter source that
includes both mGBA internals and the Parallax C ABI. It owns mGBA framebuffer
storage, converts resolved `mColor` values to explicit non-premultiplied
RGBA8, copies provenance/candidate observations, and publishes immutable frame
leases. Unity and other consumers never receive mGBA pointers or types.

The public bridge is declared in `parallax/native/include/gba_xr_bridge.h`.
It defines ownership, producer/consumer thread affinity, lease release, and
the ABI version. The current release ABI is v3.

## Safety rules for spatial rendering

1. A resolved frame lease is the byte-for-byte visual oracle.
2. A visible partition may only place pixels in the source bucket that won
   final composition; it cannot reveal occluded data.
3. Raw candidates are usable only where coverage and recomposition contracts
   prove they reproduce the resolved image at zero parallax.
4. Unsupported modes fall back to the original resolved screen.
5. Synthetic fixtures validate priorities, OBJ ordering, alpha, windows,
   brighten/darken, affine BG2, and state/input/audio contracts before game
   profiles are considered.

## Scope and non-goals

This project accepts only user-supplied content. It does not distribute games,
BIOS images, save files, or copyrighted game assets. It is an independent
fork; upstream mGBA review or acceptance is neither implied nor required for
these changes.
