[CmdletBinding()]
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\\..')).Path,
    [string]$ReadElf = 'C:\Program Files\Unity\Hub\Editor\6000.3.12f1\Editor\Data\PlaybackEngines\AndroidPlayer\NDK\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe'
)

$ErrorActionPreference = 'Stop'

function Require-Text([string]$Path, [string]$Expected) {
    $content = Get-Content -LiteralPath $Path -Raw
    if (-not $content.Contains($Expected)) {
        throw "Expected '$Expected' in $Path."
    }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Require-LoadAlignment([string]$Path) {
    $loads = @(& $ReadElf -Wl $Path | Where-Object { $_ -match '^\s*LOAD\s' })
    if ($loads.Count -eq 0 -or ($loads | Where-Object { $_ -notmatch '\s0x4000\s*$' })) {
        throw "Every ELF LOAD segment must be 16 KB aligned: $Path"
    }
}

if (-not (Test-Path -LiteralPath $ReadElf)) {
    throw "llvm-readelf was not found: $ReadElf"
}

$pluginRoot = Join-Path $ProjectRoot 'unity\Assets\Plugins\Android\GbaXrLibretroExperimental\arm64-v8a'
$manifestPath = Join-Path $pluginRoot 'package-manifest.json'
$adapterPath = Join-Path $pluginRoot 'libemuxr_mgba_libretro.so'
$corePath = Join-Path $pluginRoot 'mgba_libretro.so'
$directPath = Join-Path $ProjectRoot 'unity\Assets\Plugins\Android\arm64-v8a\libgba_xr.so'
$directMetaPath = "$directPath.meta"

foreach ($path in @($manifestPath, $adapterPath, $corePath, $directPath, $directMetaPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required packaging file is missing: $path"
    }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (-not $manifest.experimental -or $manifest.runtimeSelection -ne 'disabled') {
    throw 'The manifest must retain the experimental, runtime-disabled gate.'
}

if ((Get-Sha256 $directPath) -ne $manifest.directPluginIntegrity.sha256 -or
    (Get-Sha256 $directMetaPath) -ne $manifest.directPluginIntegrity.metaSha256) {
    throw 'The direct libgba_xr artifact or metadata changed.'
}

foreach ($artifact in $manifest.artifacts) {
    $path = Join-Path $pluginRoot $artifact.path
    if ((Get-Sha256 $path) -ne $artifact.sha256) {
        throw "Artifact hash does not match the manifest: $($artifact.path)"
    }
    Require-LoadAlignment $path
}

$adapterDynamic = (& $ReadElf -d $adapterPath) -join "`n"
if ($adapterDynamic -notmatch 'Shared library: \[mgba_libretro\.so\]') {
    throw 'The adapter is missing its required mgba_libretro.so ELF dependency.'
}

$adapterSymbols = (& $ReadElf -Ws $adapterPath) -join "`n"
if ($adapterSymbols -notmatch '\bGLOBAL\b.*emuxr_mgba_libretro_adapter_create' -or
    $adapterSymbols -match '\bGLOBAL\b.*\bgbaxr_(acquire_latest_frame|release_frame|destroy)') {
    throw 'The adapter export facade is missing or raw gbaxr bridge symbols leaked.'
}

Require-Text (Join-Path $pluginRoot 'mgba_libretro.so.meta') 'CPU: ARM64'
Require-Text (Join-Path $pluginRoot 'mgba_libretro.so.meta') 'Is16KbAligned: true'
Require-Text (Join-Path $pluginRoot 'mgba_libretro.so.meta') 'isPreloaded: 1'
Require-Text (Join-Path $pluginRoot 'libemuxr_mgba_libretro.so.meta') 'CPU: ARM64'
Require-Text (Join-Path $pluginRoot 'libemuxr_mgba_libretro.so.meta') 'Is16KbAligned: true'
Require-Text (Join-Path $pluginRoot 'libemuxr_mgba_libretro.so.meta') 'isPreloaded: 0'

Write-Output "GBA XR experimental libretro package verified: adapter=$((Get-Sha256 $adapterPath)); core=$((Get-Sha256 $corePath))"
