[CmdletBinding()]
param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$generated = Join-Path $root 'work/v00/resource-bridge/generated'
$catalog = Join-Path $generated 'catalog'
$resource = Join-Path $root 'firmware/iwatch/src/resource'
$tests = Join-Path $root 'firmware/tests'
$stub = Join-Path $tests 'v00_resource_bridge_stub'
$output = Join-Path $root 'work/v00/resource-bridge/host'

if (-not (Test-Path -LiteralPath (Join-Path $catalog 'iw_v00_resource_catalog.c'))) {
    throw '先运行 build_v00_resource_bridge.py 生成受控资源。'
}
$images = @(Get-ChildItem -LiteralPath (Join-Path $generated 'c') -Filter '*.c' -File)
if ($images.Count -ne 38) { throw "图片生成数量不符：$($images.Count)" }

if ([string]::IsNullOrWhiteSpace($VisualStudioPath)) {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioPath = & $locator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if ([string]::IsNullOrWhiteSpace($VisualStudioPath)) { throw '缺少 Visual Studio C/C++ 工具。' }
Import-Module (Join-Path $VisualStudioPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $output
try {
    $sources = @($images.FullName) + @(
        (Join-Path $catalog 'iw_v00_resource_catalog.c'),
        (Join-Path $resource 'iw_v00_resource_guard.c'),
        (Join-Path $tests 'test_v00_resource_catalog.c'))
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od "/I$stub" "/I$catalog" "/I$resource" /Fetest_v00_resource_catalog.exe @sources
    if ($LASTEXITCODE -ne 0) { throw '真实生成资源与目录的主机编译失败。' }
    & ./test_v00_resource_catalog.exe
    if ($LASTEXITCODE -ne 0) { throw '真实生成资源与目录校验失败。' }
}
finally {
    Pop-Location
}
