# Core releases

GitHub Releases publish the Android `arm64-v8a` direct mGBA adapter as a
versioned core package. Every package contains:

- `libgba_xr.so` — the instrumented direct core;
- `core-manifest.json` — ABI, platform, source-tag, and SHA-256 metadata;
- `SHA256SUMS` — an independent artifact digest.

Build one locally with:

```powershell
.\parallax\release\build-android-arm64.ps1 -Version 0.1.0-preview.1 `
  -CmakeExe "C:\\path\\to\\cmake.exe" -NdkRoot "C:\\path\\to\\Android\\NDK"
```

The output ZIP is suitable for development and for a consumer application to
package into its Android app bundle. It is **not** permission for a Google
Play build to fetch and execute a native library from GitHub at runtime.
Google Play builds must deliver native code through the store update path.

The current Parallax Emu Unity proof of concept does not yet include the
fixed-host, signature-verifying external-core loader required for direct
runtime use of a downloaded artifact. Until that loader exists and is device
validated, consumers must verify the manifest and SHA-256, then package the
core into their own approved build.

## Developer delivery manifest

`developer-delivery.json` is the small manifest consumed by Parallax Emu's
developer/sideload registry. It is deliberately separate from
`core-manifest.json`: it adds the HTTPS release asset URL while retaining the
release ZIP's SHA-256. Update and commit it with every release. It is a
download-and-verify registry input only; the current Unity application records
the archive as `DownloadedNotLoadable` and never loads or replaces a native
bridge at runtime.
