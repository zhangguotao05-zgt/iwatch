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
    $compileArgs = @('/nologo', '/std:c11', '/utf-8', '/W4', '/WX', '/Od', '/Z7',
        "/I$coreDir", '/Fetest_input_queue.exe',
        (Join-Path $PSScriptRoot 'test_input_queue.c'), (Join-Path $coreDir 'iw_input_queue.c'))
    & cl.exe @compileArgs
    if ($LASTEXITCODE -ne 0) { throw 'Host input test compilation failed.' }
    & (Join-Path $outputDir 'test_input_queue.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Host input tests failed.' }
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od /Z7 "/I$coreDir" /Fetest_gui_wait.exe (Join-Path $PSScriptRoot 'test_gui_wait.c') (Join-Path $coreDir 'iw_gui_wait.c') (Join-Path $coreDir 'iw_input_queue.c')
    if ($LASTEXITCODE -ne 0) { throw 'GUI 等待测试编译失败。' }
    & ./test_gui_wait.exe
    if ($LASTEXITCODE -ne 0) { throw 'GUI 等待与恢复测试失败。' }
    $platformDir = Join-Path $firmwareDir 'iwatch/src/platform'
    & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /Od "/I$coreDir" "/I$platformDir" "/I$PSScriptRoot/mocks" /Fetest_gui_port.exe (Join-Path $PSScriptRoot 'test_gui_port.c') (Join-Path $platformDir 'iw_gui_port.c') (Join-Path $coreDir 'iw_gui_wait.c')
    if ($LASTEXITCODE -ne 0) { throw 'GUI 事件适配测试编译失败。' }
    & ./test_gui_port.exe
    if ($LASTEXITCODE -ne 0) { throw 'GUI 事件适配测试失败。' }
    & py -3 (Join-Path $PSScriptRoot 'generate_lifecycle_test.py')
    if ($LASTEXITCODE -ne 0) { throw '生命周期测试生成失败。' }
    foreach ($page in @('clock', 'menu', 'status', 'simple', 'dial', 'rotate_bg')) {
        # 替身中的空回调允许未使用参数；AddressSanitizer 检查实际生产回收函数。
        & cl.exe /nologo /std:c11 /utf-8 /W4 /WX /wd4505 /wd4100 /wd4189 /D_CRT_SECURE_NO_WARNINGS /fsanitize=address /Zi "/Fetest_${page}_lifecycle.exe" "test_${page}_lifecycle.c"
        if ($LASTEXITCODE -ne 0) { throw "$page 生命周期测试编译失败。" }
        & "./test_${page}_lifecycle.exe"
        if ($LASTEXITCODE -ne 0) { throw "$page 生命周期测试失败。" }
    }
    & py -3 -m unittest discover -s $PSScriptRoot -p 'test_*.py' -v
    if ($LASTEXITCODE -ne 0) { throw 'Layout regression tests failed.' }
    & (Join-Path $PSScriptRoot 'test-build-lock.ps1')
}
finally { Pop-Location }
