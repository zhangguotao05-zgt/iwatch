[CmdletBinding()]
param([string]$VisualStudioPath)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($VisualStudioPath)) {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $locator)) {
        throw 'Visual Studio C/C++ tools are required, or pass -VisualStudioPath.'
    }
    $VisualStudioPath = & $locator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if ([string]::IsNullOrWhiteSpace($VisualStudioPath)) { throw 'Visual Studio C/C++ tools were not found.' }
Import-Module (Join-Path $VisualStudioPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

$firmwareDir = Split-Path -Parent $PSScriptRoot
$coreDir = Join-Path $firmwareDir 'iwatch\src\core'
$outputDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
Push-Location $outputDir
try {
    $compileArgs = @('/nologo', '/std:c11', '/W4', '/WX', '/Od', '/Z7',
        "/I$coreDir", '/Fetest_input_queue.exe',
        (Join-Path $PSScriptRoot 'test_input_queue.c'), (Join-Path $coreDir 'iw_input_queue.c'))
    & cl.exe @compileArgs
    if ($LASTEXITCODE -ne 0) { throw 'Host input test compilation failed.' }
    & (Join-Path $outputDir 'test_input_queue.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Host input tests failed.' }
    & py -3 -m unittest discover -s $PSScriptRoot -p test_layout.py -v
    if ($LASTEXITCODE -ne 0) { throw 'Layout regression tests failed.' }
}
finally { Pop-Location }
