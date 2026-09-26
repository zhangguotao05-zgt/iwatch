[CmdletBinding()]
param(
    [string]$GccPath = 'arm-none-eabi-gcc.exe',
    [string]$KeilRoot = $env:IWATCH_KEIL_ROOT
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$generated = Join-Path $root 'work/v00/resource-bridge/generated'
$catalog = Join-Path $generated 'catalog'
$resource = Join-Path $root 'firmware/iwatch/src/resource'
$stub = Join-Path $root 'firmware/tests/v00_resource_bridge_stub'
$targetTest = Join-Path $root 'firmware/tests/test_v00_resource_target_link.c'
$sources = @(Get-ChildItem -LiteralPath (Join-Path $generated 'c') -Filter '*.c' -File).FullName
if ($sources.Count -ne 38) { throw 'V00 图片资源尚未完整生成。' }
$sources += @(Join-Path $catalog 'iw_v00_resource_catalog.c')
$sources += @(Join-Path $resource 'iw_v00_resource_guard.c')
$sources += @($targetTest)
$includes = @("-I$stub", "-I$catalog", "-I$resource")

if (-not (Test-Path -LiteralPath $GccPath)) { throw 'ARM GCC 不存在。' }
$armclang = Join-Path $KeilRoot 'ARMCLANG/bin/armclang.exe'
$armlink = Join-Path $KeilRoot 'ARMCLANG/bin/armlink.exe'
if (-not (Test-Path -LiteralPath $armclang) -or -not (Test-Path -LiteralPath $armlink)) {
    throw 'Keil ARM Compiler 6 不存在。'
}

foreach ($toolchain in @('gcc', 'keil')) {
    $output = Join-Path $root "work/v00/resource-bridge/$toolchain-link"
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $objects = @()
    foreach ($source in $sources) {
        $name = [IO.Path]::GetFileNameWithoutExtension($source)
        $object = Join-Path $output ($name + '.o')
        if ($toolchain -eq 'gcc') {
            & $GccPath -std=c11 -mcpu=cortex-m33 -mthumb -Os -ffunction-sections -fdata-sections -Wall -Wextra -Werror @includes -c $source -o $object
        }
        else {
            & $armclang --target=arm-arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 -Oz -ffunction-sections -fdata-sections -Wall -Wextra -Werror @includes -c $source -o $object
        }
        if ($LASTEXITCODE -ne 0) { throw "$toolchain 编译失败：$source" }
        $objects += $object
    }
    $map = Join-Path $output 'v00-resource-probe.map'
    if ($toolchain -eq 'gcc') {
        $image = Join-Path $output 'v00-resource-probe.elf'
        & $GccPath -mcpu=cortex-m33 -mthumb -nostdlib '-Wl,--gc-sections,--entry=_start,-Ttext=0x08000000' "-Wl,-Map=$map" -o $image @objects -lgcc
    }
    else {
        $image = Join-Path $output 'v00-resource-probe.axf'
        & $armlink --cpu Cortex-M33 --entry _start --ro-base 0x08000000 --remove --map "--list=$map" "--output=$image" @objects
    }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $image)) {
        throw "$toolchain 最小目标链接失败。"
    }
    Write-Output "$toolchain minimal target link OK: $image"
}
