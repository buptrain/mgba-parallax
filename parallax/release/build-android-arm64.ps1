param(
    [Parameter(Mandatory)] [string]$Version,
    [Parameter(Mandatory)] [string]$CmakeExe,
    [Parameter(Mandatory)] [string]$NdkRoot,
    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\\..")).Path
$native = Join-Path $root "parallax\\native"
$build = Join-Path $root "build\\android-arm64-v8a-release"
$output = Join-Path $root $OutputDirectory
$toolchain = Join-Path $NdkRoot "build\\cmake\\android.toolchain.cmake"
$ninja = Join-Path (Split-Path $CmakeExe -Parent) "ninja.exe"

foreach ($path in @($CmakeExe, $NdkRoot, $toolchain, $ninja)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required build path was not found: $path" }
}

& $CmakeExe -S $native -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 `
    -DGBAXR_BUILD_MGBA_ADAPTER=ON -DGBAXR_BUILD_MGBA_LIBRETRO_CORE=OFF -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

& $CmakeExe --build $build --target gba_xr_mgba_adapter
if ($LASTEXITCODE -ne 0) { throw "Core build failed." }

$library = Join-Path $build "libgba_xr.so"
if (-not (Test-Path -LiteralPath $library)) { throw "Expected core artifact was not produced: $library" }

$packageName = "mgba-parallax-v$Version-android-arm64-v8a"
$package = Join-Path $output $packageName
New-Item -ItemType Directory -Force -Path $package | Out-Null
Copy-Item -LiteralPath $library -Destination (Join-Path $package "libgba_xr.so") -Force
$hash = (Get-FileHash -LiteralPath (Join-Path $package "libgba_xr.so") -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    schema = "parallax.core.manifest/v1"
    coreId = "mgba-parallax"
    version = $Version
    abiVersion = 3
    platform = "android-arm64-v8a"
    artifact = "libgba_xr.so"
    sha256 = $hash
    source = [ordered]@{
        repository = "https://github.com/buptrain/mgba-parallax"
        tag = "v$Version"
        upstreamCommit = "a67176cd3e6bf60a5016d7adc94357142dd12617"
    }
    runtime = [ordered]@{
        requiresCoreHostApi = 1
        googlePlayRuntimeDownloadAllowed = $false
    }
}
[System.IO.File]::WriteAllText((Join-Path $package "core-manifest.json"), ($manifest | ConvertTo-Json -Depth 5) + [Environment]::NewLine)
[System.IO.File]::WriteAllText((Join-Path $package "SHA256SUMS"), "$hash  libgba_xr.so$([Environment]::NewLine)")
$zip = Join-Path $output "$packageName.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $zip
Write-Host "Built release package: $zip"
