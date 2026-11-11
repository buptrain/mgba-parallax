# mGBA Parallax instrumentation

This repository is the public source and release boundary for the Parallax
Emu mGBA integration. It is based on upstream mGBA at commit
`a67176cd3e6bf60a5016d7adc94357142dd12617` and retains the upstream source
history and MPL-2.0 license.

The Parallax-specific work lives under `parallax/`:

- `patches/` contains the reviewable final-pixel provenance patch, already
  applied to this checkout.
- `native/` contains the direct mGBA adapter, bridge implementation, audio
  handoff, state boundary, libretro experiment, and contract tests.
- `fixtures/gba/` contains source-only synthetic GBA test programs. It never
  contains commercial ROMs, BIOS images, saves, or generated ROM files.

## Build

Run the platform-independent contract tests:

```powershell
cmake -S parallax/native -B build/parallax -DBUILD_TESTING=ON
cmake --build build/parallax --config Debug
ctest --test-dir build/parallax -C Debug --output-on-failure
```

To build the direct mGBA adapter, configure the same source with
`-DGBAXR_BUILD_MGBA_ADAPTER=ON`. The default `MGBA_ROOT` is this repository;
the Android toolchain and output packaging are deliberately owned by the
consumer application.

## Distribution boundary

Every binary release must name this repository tag, the upstream mGBA commit,
the Parallax ABI version, toolchain identity, and SHA-256. Corresponding
source is this exact tag. Consumer applications must carry their own notices
and must not ship ROMs or BIOS files from this repository.

`parallax/native` is kept under MPL-2.0 in this initial repository so the
instrumented core and its adapter have one unambiguous source-license
boundary. A future permissively licensed multi-core SDK may be split out only
after its files and license ownership have been reviewed.
