[CmdletBinding()]
param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$source = Join-Path $root 'firmware/iwatch/src/resource'
$output = Join-Path $root 'work/v00/resource-bridge/background'
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
    & cl.exe /nologo /LD /O2 /std:c11 /utf-8 /W4 /WX "/I$source" /Feiw_v00_control_background.dll (Join-Path $source 'iw_v00_control_background.c') /link /EXPORT:iw_v00_control_background_line
    if ($LASTEXITCODE -ne 0) { throw '控制中心背景 C 样片编译失败。' }
    & python -X utf8 (Join-Path $root 'tools/design/test_v00_background.py') --library (Join-Path $output 'iw_v00_control_background.dll')
    if ($LASTEXITCODE -ne 0) { throw '控制中心背景样片与批准稿对照失败。' }
}
finally { Pop-Location }
