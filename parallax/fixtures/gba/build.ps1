param(
    [string]$ToolchainBin = "C:\Program Files\Unity\Hub\Editor\6000.3.12f1\Editor\Data\PlaybackEngines\AndroidPlayer\NDK\toolchains\llvm\prebuilt\windows-x86_64\bin",
    [ValidateSet("frame_sequence", "input_state_replay", "bg0_priority", "bg_tie", "obj_priority", "alpha_blend", "brighten", "darken", "window_alpha", "affine_bg2", "obj_alpha")]
    [string]$Fixture = "frame_sequence",
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$out = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { Join-Path $root "out" } else { $OutputDirectory }
$clang = Join-Path $ToolchainBin "clang.exe"
$linker = Join-Path $ToolchainBin "ld.lld.exe"
$objcopy = Join-Path $ToolchainBin "llvm-objcopy.exe"

foreach ($tool in @($clang, $linker, $objcopy)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Required LLVM tool was not found: $tool"
    }
}

New-Item -ItemType Directory -Force -Path $out | Out-Null
$source = Join-Path $root "$Fixture.s"
$object = Join-Path $out "$Fixture.o"
$elf = Join-Path $out "$Fixture.elf"
$rom = Join-Path $out "$Fixture.gba"

if (-not (Test-Path -LiteralPath $source)) {
    throw "Fixture source was not found: $source"
}

& $clang --target=arm-none-eabi -mcpu=arm7tdmi -marm -ffreestanding -fno-pic -c $source -o $object
& $linker -T (Join-Path $root "linker.ld") -o $elf $object
& $objcopy -O binary $elf $rom

Write-Host "Built synthetic fixture: $rom"
